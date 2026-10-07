#include "StrobedSwitchMatrix.h"

#include "SwitchMatrixPIO/StrobedMatrix8x16.pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"

StrobedSwitchMatrix* StrobedSwitchMatrix::instance = nullptr;

namespace {

// The scan program is written for nine strobe GPIOs and sixteen returns. A
// board with a different span needs a different program, not a different
// number here.
constexpr uint8_t kProgramStrobeSpan = 9;
constexpr uint8_t kProgramReturns = 16;

// PIO cycles per second. One slot is 42 cycles, so this gives a strobe about
// every 105 us with 80 us to settle before the returns are sampled, and the
// whole matrix just under once a millisecond.
constexpr float kScanClockHz = 400000.0f;

}  // namespace

bool StrobedSwitchMatrix::registerSwitch(byte p, byte n) {
  if (profile.strobeSpan != kProgramStrobeSpan ||
      profile.maxReturns != kProgramReturns) {
    return false;
  }
  if (!logic.registerSwitch(p, n)) {
    return false;
  }
  active = true;
  return true;
}

// Runs in interrupt context, whenever the scan has pushed a sample.
void StrobedSwitchMatrix::onSamples() {
  if (instance && instance->pio) {
    instance->drainSamples();
  }
}

void StrobedSwitchMatrix::drainSamples() {
  StrobedSwitchMatrixLogic::Edge edges[StrobedSwitchMatrixLogic::kMaxRows];

  // The interrupt is the FIFO's "not empty" level, so it clears itself once
  // this loop has emptied it.
  while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
    const uint32_t word = pio_sm_get(pio, sm);
    const uint8_t count = logic.decode(word, time_us_64(), edges,
                                       StrobedSwitchMatrixLogic::kMaxRows);
    for (uint8_t i = 0; i < count; i++) {
      // Keep the full edge sequence so a short pulse is not collapsed to its
      // final state before the main loop gets to it.
      const uint8_t nextHead = static_cast<uint8_t>(
          (pendingEventHead + 1) % STROBED_MATRIX_EVENT_QUEUE_SIZE);
      if (nextHead != pendingEventTail) {
        pendingEvents[pendingEventHead] = edges[i];
        pendingEventHead = nextHead;
      }
    }
  }
}

void StrobedSwitchMatrix::startReader() {
  if (running || !active) {
    return;
  }

  if (!programLoaded) {
    PioSlot slot;
    slot.program = &strobed_matrix_8x16_pio_program;
    if (!pioClaimSlots(&slot, 1)) {
      // No state machine or program space left. Say so where it can be seen -
      // EVENT_ERROR fast-blinks the on-board LED - rather than leaving a
      // matrix that never reports a switch.
      _eventDispatcher->dispatch(new Event(EVENT_ERROR));
      return;
    }
    pio = slot.pio;
    sm = slot.sm;
    programOffset = slot.offset;
    programLoaded = true;
  }

  instance = this;
  running = true;

  pio_sm_config c =
      strobed_matrix_8x16_pio_program_get_default_config(programOffset);
  sm_config_set_out_pins(&c, profile.strobeBasePin, profile.strobeSpan);
  sm_config_set_in_pins(&c, profile.returnsBasePin);
  sm_config_set_out_shift(&c, true, false, 32);
  sm_config_set_in_shift(&c, false, false, 32);
  // Nothing is ever sent to this state machine, so give the reader both
  // halves of the FIFO: eight samples of slack while interrupts are off.
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / kScanClockHz);

  // Only the real strobes. The GPIO in the middle of their range is the
  // on-board LED; left with its own function, the scan's slot for it drives
  // nothing.
  for (uint8_t i = 0; i < profile.strobes; i++) {
    pio_gpio_init(pio, profile.strobePins[i]);
    pio_sm_set_consecutive_pindirs(pio, sm, profile.strobePins[i], 1, true);
  }
  for (uint8_t i = 0; i < profile.maxReturns; i++) {
    pio_gpio_init(pio, profile.returnsBasePin + i);
  }
  pio_sm_set_consecutive_pindirs(pio, sm, profile.returnsBasePin,
                                 profile.maxReturns, false);

  pio_sm_init(pio, sm, programOffset, &c);

  const int irqNum = pio_get_irq_num(pio, 0);
  irq_set_exclusive_handler(irqNum, onSamples);
  irq_set_enabled(irqNum, true);
  pio_set_irq0_source_enabled(
      pio, static_cast<pio_interrupt_source_t>(pis_sm0_rx_fifo_not_empty + sm),
      true);
  pio_sm_set_enabled(pio, sm, true);
}

void StrobedSwitchMatrix::stopReader() {
  if (!running) {
    return;
  }

  const int irqNum = pio_get_irq_num(pio, 0);
  pio_sm_set_enabled(pio, sm, false);
  pio_set_irq0_source_enabled(
      pio, static_cast<pio_interrupt_source_t>(pis_sm0_rx_fifo_not_empty + sm),
      false);
  irq_set_enabled(irqNum, false);
  // Take the handler off as well: a later start may land on the other PIO
  // block, and this one would otherwise still fire into a stopped matrix.
  irq_remove_handler(irqNum, onSamples);

  // The state machine stopped with one strobe still selected. Release it.
  uint32_t strobeMask = 0;
  for (uint8_t i = 0; i < profile.strobes; i++) {
    strobeMask |= 1u << profile.strobePins[i];
  }
  pio_sm_set_pins_with_mask(pio, sm, 0, strobeMask);
  pio_sm_clear_fifos(pio, sm);

  if (instance == this) {
    instance = nullptr;
  }
  running = false;
}

void StrobedSwitchMatrix::releaseProgram() {
  if (!programLoaded) {
    return;
  }

  PioSlot slot;
  slot.program = &strobed_matrix_8x16_pio_program;
  slot.pio = pio;
  slot.sm = sm;
  slot.offset = programOffset;
  slot.claimed = true;
  pioReleaseSlots(&slot, 1);

  programLoaded = false;
  programOffset = 0;
  pio = nullptr;
  sm = 0;
}

void StrobedSwitchMatrix::resendStableStates() {
  StrobedSwitchMatrixLogic::Edge states[StrobedSwitchMatrixLogic::kMaxColumns *
                                        StrobedSwitchMatrixLogic::kMaxRows];
  const uint8_t count =
      logic.snapshot(states, StrobedSwitchMatrixLogic::kMaxColumns *
                                 StrobedSwitchMatrixLogic::kMaxRows);
  for (uint8_t i = 0; i < count; i++) {
    _eventDispatcher->dispatch(new Event(
        EVENT_SOURCE_SWITCH, word(0, states[i].number), states[i].state));
  }
}

void StrobedSwitchMatrix::resetConfig() {
  stopReader();
  releaseProgram();

  logic.reset();
  active = false;
  pendingEventHead = 0;
  pendingEventTail = 0;
  memset(pendingEvents, 0, sizeof(pendingEvents));
}

void StrobedSwitchMatrix::handleEvent(Event* event) {
  switch (event->sourceId) {
    case EVENT_POLL_EVENTS: {
      while (true) {
        StrobedSwitchMatrixLogic::Edge pending;
        const uint32_t irqState = save_and_disable_interrupts();
        if (pendingEventTail == pendingEventHead) {
          restore_interrupts(irqState);
          break;
        }
        pending = pendingEvents[pendingEventTail];
        pendingEventTail = static_cast<uint8_t>(
            (pendingEventTail + 1) % STROBED_MATRIX_EVENT_QUEUE_SIZE);
        restore_interrupts(irqState);

        _eventDispatcher->dispatch(new Event(
            EVENT_SOURCE_SWITCH, word(0, pending.number), pending.state));
      }
      break;
    }

    case EVENT_READ_SWITCHES:
      // The CPU requested all current states, usually when the game starts.
      // Report what each switch is actually in; the scan only reports changes
      // from there on.
      if (active) {
        resendStableStates();
        startReader();
      }
      break;

    case EVENT_REFRESH_SWITCHES:
      if (active) {
        stopReader();
        resendStableStates();
        startReader();
      }
      break;
  }
}
