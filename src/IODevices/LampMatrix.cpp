#include "LampMatrix.h"

#include "../SafeOff.h"
#include "LampMatrixPIO/LampMatrix.pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

namespace {

// The strobe program shifts out this many pins from the lowest lamp GPIO.
constexpr uint8_t kProgramPinSpan = 22;

// PIO cycles per second. One column slot is 133 cycles, so a column is lit for
// 252 us out of every 2.1 ms: the whole matrix refreshes about 470 times a
// second.
constexpr float kStrobeClockHz = 500000.0f;

// How often the feeder tops up the FIFO. The FIFO holds eight column words,
// a little over 2 ms of strobing, so one missed tick costs nothing.
constexpr int64_t kFeedIntervalUs = 1000;

uint8_t lowestPin(uint32_t mask) {
  uint8_t pin = 0;
  while (pin < 32 && ((mask >> pin) & 1u) == 0) {
    pin++;
  }
  return pin;
}

uint8_t highestPin(uint32_t mask) {
  uint8_t pin = 31;
  while (pin > 0 && ((mask >> pin) & 1u) == 0) {
    pin--;
  }
  return pin;
}

}  // namespace

LampMatrix::LampMatrix(EventDispatcher* eD, const ppuc::board::Profile& p)
    : _eventDispatcher(eD) {
  lampPins = p.lampPins;
  basePin = lowestPin(lampPins);
  pinSpan = static_cast<uint8_t>(highestPin(lampPins) - basePin + 1);
  columns = p.lampMatrix.columns;
  logic.configure(p.lampMatrix, lampPins, basePin);
  _eventDispatcher->addListener(this, EVENT_SOURCE_LIGHT);
}

bool LampMatrix::registerLamp(byte position, byte number) {
  if (pinSpan != kProgramPinSpan || !logic.registerLamp(position, number)) {
    return false;
  }
  active = true;
  return true;
}

bool LampMatrix::registerDirect(byte pin, byte number) {
  if (pinSpan != kProgramPinSpan || !logic.registerDirect(pin, number)) {
    return false;
  }
  active = true;
  return true;
}

// Hands the lamp GPIOs to the PIO block, as outputs.
//
// Every lamp GPIO, not only the configured ones: an output nothing is
// registered on is then actively held off instead of left floating.
void LampMatrix::claimPins() {
  for (uint8_t pin = 0; pin < 32; pin++) {
    if ((lampPins >> pin) & 1u) {
      pio_gpio_init(pio, pin);
    }
  }
  pio_sm_set_pindirs_with_mask(pio, sm, lampPins, lampPins);
  pinsTaken = false;
}

void LampMatrix::start() {
  if (started || !active) {
    return;
  }

  PioSlot slot;
  slot.program = &lamp_matrix_pio_program;
  if (!pioClaimSlots(&slot, 1)) {
    // EVENT_ERROR fast-blinks the on-board LED. Better than a board whose
    // lamps silently stay dark.
    _eventDispatcher->dispatch(new Event(EVENT_ERROR));
    return;
  }
  pio = slot.pio;
  sm = slot.sm;
  programOffset = slot.offset;

  pio_sm_config c = lamp_matrix_pio_program_get_default_config(programOffset);
  sm_config_set_out_pins(&c, basePin, pinSpan);
  sm_config_set_out_shift(&c, true, false, 32);
  // Nothing is ever read from this state machine, so give the feeder both
  // halves of the FIFO: a whole frame of eight columns.
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / kStrobeClockHz);

  // Outputs low before the pins change hands, so nothing flashes.
  pio_sm_set_pins_with_mask(pio, sm, 0, lampPins);
  claimPins();
  pio_sm_init(pio, sm, programOffset, &c);
  pio_sm_set_enabled(pio, sm, true);

  nextColumn = 0;
  started = true;
  add_repeating_timer_us(-kFeedIntervalUs, onFeed, this, &feedTimer);
}

void LampMatrix::stop() {
  if (!started) {
    return;
  }

  cancel_repeating_timer(&feedTimer);
  pio_sm_set_enabled(pio, sm, false);
  pio_sm_clear_fifos(pio, sm);
  // The state machine may have stopped mid-column.
  pio_sm_set_pins_with_mask(pio, sm, 0, lampPins);

  PioSlot slot;
  slot.program = &lamp_matrix_pio_program;
  slot.pio = pio;
  slot.sm = sm;
  slot.offset = programOffset;
  slot.claimed = true;
  pioReleaseSlots(&slot, 1);

  pio = nullptr;
  sm = 0;
  programOffset = 0;
  started = false;
}

bool LampMatrix::onFeed(struct repeating_timer* t) {
  static_cast<LampMatrix*>(t->user_data)->feed();
  return true;
}

// Runs in interrupt context, once a millisecond.
void LampMatrix::feed() {
  if (g_outputsForcedOff) {
    // The watchdog has taken the pins and is holding them low. Feeding the
    // state machine would change nothing; remember to take the pins back.
    pinsTaken = true;
    return;
  }
  if (pinsTaken) {
    pio_sm_clear_fifos(pio, sm);
    claimPins();
  }
  if (!lit) {
    // An unfed state machine drives every output low by itself.
    return;
  }
  while (!pio_sm_is_tx_fifo_full(pio, sm)) {
    pio_sm_put(pio, sm, logic.columnWord(nextColumn));
    nextColumn = static_cast<uint8_t>((nextColumn + 1) % columns);
  }
}

void LampMatrix::setRunning(bool run) {
  if (run) {
    start();
    lit = started;
    return;
  }
  if (lit) {
    // Dark, but the lamp states are kept: the host only sends changes, so a
    // lamp forgotten here would stay off after the game resumes.
    lit = false;
    if (started) {
      pio_sm_clear_fifos(pio, sm);
    }
  }
}

void LampMatrix::resetConfig() {
  lit = false;
  stop();
  logic.reset();
  active = false;
}

void LampMatrix::handleEvent(Event* event) {
  if (event->sourceId == EVENT_SOURCE_LIGHT) {
    logic.setLamp((byte)event->eventId, (bool)event->value);
  }
}
