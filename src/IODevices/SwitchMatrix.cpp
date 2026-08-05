#include "SwitchMatrix.h"

#include "SwitchMatrixPIO/ActiveHigh4Columns.pio.h"
#include "SwitchMatrixPIO/ActiveHigh4Rows.pio.h"
#include "SwitchMatrixPIO/ActiveHigh8Rows.pio.h"
#include "SwitchMatrixPIO/ActiveLow4Columns.pio.h"
#include "SwitchMatrixPIO/ActiveLow4Rows.pio.h"
#include "SwitchMatrixPIO/ActiveLow8Rows.pio.h"

SwitchMatrix* SwitchMatrix::instance = nullptr;

namespace {

// One place decides which program a given geometry needs, so the load and the
// unload can never disagree about what is on the block.
const pio_program_t* matrixColumnsProgram(bool activeLow) {
  return activeLow ? &active_low_4_columns_pio_program
                   : &active_high_4_columns_pio_program;
}

const pio_program_t* matrixRowsProgram(bool activeLow, uint8_t numRows) {
  if (activeLow) {
    return (4 == numRows) ? &active_low_4_rows_pio_program
                          : &active_low_8_rows_pio_program;
  }
  return (4 == numRows) ? &active_high_4_rows_pio_program
                        : &active_high_8_rows_pio_program;
}

}  // namespace

bool SwitchMatrix::supportsRows(uint8_t n) const {
  if (n == 0 || n > profile.maxRows || n >= 16) {
    return false;
  }
  return (profile.supportedRowsMask & (1u << n)) != 0;
}

bool SwitchMatrix::setNumRows(uint8_t n) {
  if (!supportsRows(n)) {
    active = false;
    return false;
  }
  numRows = n;
  return true;
}

bool SwitchMatrix::isConfiguredGeometrySupported() const {
  return profile.columns == 4 && profile.columns <= SWITCH_MATRIX_MAX_COLUMNS &&
         supportsRows(numRows);
}

void SwitchMatrix::stopReader() {
  if (!running) {
    return;
  }

  const int irqNum = pio_get_irq_num(pio, 0);
  pio_sm_set_enabled(pio, sm_columns, false);
  pio_sm_set_enabled(pio, sm_rows, false);
  pio_set_irq0_source_enabled(pio, pis_interrupt0, false);
  irq_set_enabled(irqNum, false);
  // Take the handler off as well: a later start may land on the other PIO
  // block, and this one would otherwise still fire into a stopped matrix.
  irq_remove_handler(irqNum, onRowChanges);
  if (instance == this) {
    instance = nullptr;
  }
  running = false;
}

void SwitchMatrix::releasePrograms() {
  if (!columnsProgramLoaded && !rowsProgramLoaded) {
    return;
  }

  PioSlot slots[2];
  uint count = 0;

  if (columnsProgramLoaded) {
    slots[count].program = matrixColumnsProgram(loadedActiveLow);
    slots[count].pio = pio;
    slots[count].sm = sm_columns;
    slots[count].offset = columnsProgramOffset;
    slots[count].claimed = true;
    count++;
  }
  if (rowsProgramLoaded) {
    slots[count].program = matrixRowsProgram(loadedActiveLow, loadedNumRows);
    slots[count].pio = pio;
    slots[count].sm = sm_rows;
    slots[count].offset = rowsProgramOffset;
    slots[count].claimed = true;
    count++;
  }

  pioReleaseSlots(slots, count);

  columnsProgramLoaded = false;
  rowsProgramLoaded = false;
  columnsProgramOffset = 0;
  rowsProgramOffset = 0;
  pio = nullptr;
  sm_columns = 0;
  sm_rows = 0;
}

// Sets lastStable to what an all-open matrix reads as, for the configured
// polarity and geometry.
//
// lastStable holds raw samples, and on an active-low matrix an open switch
// reads 1. Left at zero, resendStableStates() would tell the host every mapped
// switch was closed - every target, every rollover, the trough - which is what
// EVENT_REFRESH_SWITCHES would have reported before the first scan landed.
//
// Only meaningful before a real sample exists; afterwards lastStable is the
// truth and must not be overwritten, or restarting the reader would lose the
// live state.
void SwitchMatrix::seedIdleStableState() {
  const uint8_t bits =
      static_cast<uint8_t>(profile.columns * numRows);
  const uint32_t used =
      (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
  lastStable = activeLow ? used : 0u;
}

void SwitchMatrix::startReader() {
  if (running || !active || !isConfiguredGeometrySupported()) {
    return;
  }

  if (!haveScanned) {
    // Polarity and row count are known by now; at construction they were not.
    seedIdleStableState();
  }

  instance = this;
  running = true;

  const bool reuseLoadedPrograms =
      columnsProgramLoaded && rowsProgramLoaded &&
      loadedActiveLow == activeLow && loadedNumRows == numRows;

  if (!reuseLoadedPrograms) {
    // Polarity or row count changed, so the loaded programs are the wrong
    // ones. Give them back before claiming the new pair: the previous code
    // added the new programs on top and leaked the old instruction space on
    // every reconfiguration.
    releasePrograms();

    PioSlot slots[2];
    slots[0].program = matrixColumnsProgram(activeLow);
    slots[1].program = matrixRowsProgram(activeLow, numRows);
    if (!pioClaimSlots(slots, 2)) {
      // No single block can host both state machines. Report it where it can
      // be seen - EVENT_ERROR fast-blinks the on-board LED - rather than
      // leaving a matrix that never reports a switch.
      _eventDispatcher->dispatch(new Event(EVENT_ERROR));
      instance = nullptr;
      running = false;
      return;
    }

    pio = slots[0].pio;
    sm_columns = slots[0].sm;
    columnsProgramOffset = slots[0].offset;
    sm_rows = slots[1].sm;
    rowsProgramOffset = slots[1].offset;
    columnsProgramLoaded = true;
    rowsProgramLoaded = true;
    loadedActiveLow = activeLow;
    loadedNumRows = numRows;
  }

  const uint columns_offset = columnsProgramOffset;
  const uint rows_offset = rowsProgramOffset;

  pio_sm_config c_columns =
      loadedActiveLow
          ? active_low_4_columns_pio_program_get_default_config(columns_offset)
          : active_high_4_columns_pio_program_get_default_config(
                columns_offset);

  pio_sm_config c_rows;
  if (loadedActiveLow) {
    c_rows =
        (4 == loadedNumRows)
            ? active_low_4_rows_pio_program_get_default_config(rows_offset)
            : active_low_8_rows_pio_program_get_default_config(rows_offset);
  } else {
    c_rows =
        (4 == loadedNumRows)
            ? active_high_4_rows_pio_program_get_default_config(rows_offset)
            : active_high_8_rows_pio_program_get_default_config(rows_offset);
  }

  sm_config_set_in_pins(&c_columns, profile.columnsBasePin);
  for (uint i = 0; i < profile.columns; i++) {
    pio_gpio_init(pio, profile.columnsBasePin + i);
  }
  pio_sm_set_consecutive_pindirs(pio, sm_columns, profile.columnsBasePin,
                                 profile.columns, true);
  sm_config_set_out_shift(&c_columns, false, false, 0);
  pio_sm_init(pio, sm_columns, columns_offset, &c_columns);
  pio_sm_set_enabled(pio, sm_columns, true);

  const uint8_t rowsBasePin = profile.columnsBasePin - numRows;
  sm_config_set_in_pins(&c_rows, rowsBasePin);

  // Only the row pins. The rows program also waits on the column pins - they
  // sit directly above the rows, which is why in_base covers both and the
  // program refers to them as PIN numRows..numRows+columns-1 - but it only
  // *reads* them, and PIO input sampling reads the pad whatever its direction.
  //
  // Claiming them here would take them back: pin direction belongs to the PIO
  // block, not the state machine, and both machines share a block. This setup
  // runs after the columns machine has set the same pins to output, so
  // including them made the last writer win and left the columns as inputs.
  // The columns then never drove, the rows program waited forever on its first
  // `wait 0 PIN`, and no scan ever completed.
  for (uint i = 0; i < numRows; i++) {
    pio_gpio_init(pio, rowsBasePin + i);
  }
  pio_sm_set_consecutive_pindirs(pio, sm_rows, rowsBasePin, numRows, false);
  sm_config_set_in_shift(&c_rows, false, false, 0);
  pio_sm_init(pio, sm_rows, rows_offset, &c_rows);
  const int irqNum = pio_get_irq_num(pio, 0);
  irq_set_exclusive_handler(irqNum, onRowChanges);
  irq_set_enabled(irqNum, true);
  pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
  pio_sm_set_enabled(pio, sm_rows, true);
}

void SwitchMatrix::resendStableStates() {
  for (int column = 0; column < profile.columns; column++) {
    for (int row = 0; row < numRows; row++) {
      const uint8_t pos = column * numRows + row;
      if (mapping[pos] == 0) {
        continue;
      }

      const uint32_t mask = 1u << ((profile.columns - 1 - column) * numRows + row);
      const bool rawBit = (lastStable & mask) != 0;
      const bool switchState = activeLow ? !rawBit : rawBit;
      _eventDispatcher->dispatch(
          new Event(EVENT_SOURCE_SWITCH, word(0, mapping[pos]),
                    switchState ? 1 : 0));
    }
  }
}

void SwitchMatrix::resetConfig() {
  stopReader();

  releasePrograms();

  activeLow = false;
  numRows = profile.maxRows >= 4 ? 4 : profile.maxRows;
  loadedActiveLow = false;
  loadedNumRows = 4;
  active = false;
  memset(mapping, 0, sizeof(mapping));
  lastStable = 0;
  haveScanned = false;
  pendingEventHead = 0;
  pendingEventTail = 0;
  memset(pendingEvents, 0, sizeof(pendingEvents));
  memset(debounceTime, 0, sizeof(debounceTime));
}

void SwitchMatrix::registerSwitch(byte p, byte n) {
  if (isConfiguredGeometrySupported() && p < (profile.columns * numRows)) {
    mapping[p] = n;
    active = true;
  }
}

void SwitchMatrix::handleRowChanges(uint32_t raw) {
  absolute_time_t now = get_absolute_time();
  haveScanned = true;
  uint32_t changed = raw ^ lastStable;  // raw to raw comparison

  for (int column = 0; column < profile.columns; column++) {
    for (int row = 0; row < numRows; row++) {
      uint8_t pos = column * numRows + row;
      if (mapping[pos] == 0) continue;  // Not registered

      uint32_t mask = 1u << ((profile.columns - 1 - column) * numRows + row);

      if (changed & mask) {
        // Convert RAW to logical pressed/released
        // -----------------------------------------
        // activeLow : pressed = raw_bit == 0
        // activeHigh: pressed = raw_bit == 1
        bool rawBit = (raw & mask) != 0;
        bool switchState = activeLow ? (!rawBit)  // active-low: 0 = pressed
                                     : rawBit;    // active-high: 1 = pressed
        // Debounce
        if (absolute_time_diff_us(debounceTime[pos][switchState], now) >=
            MATRIX_SWITCH_DEBOUNCE * 1000) {
          debounceTime[pos][switchState] = now;
          // Store the *raw* stable state
          if (rawBit)
            lastStable |= mask;  // raw=1
          else
            lastStable &= ~mask;  // raw=0

          // Preserve the full edge sequence in a fixed-size ring buffer so a
          // short pulse is not collapsed to only its final state before the
          // normal loop flushes events.
          const uint8_t nextHead =
              static_cast<uint8_t>((pendingEventHead + 1) %
                                   MATRIX_SWITCH_EVENT_QUEUE_SIZE);
          if (nextHead != pendingEventTail) {
            pendingEvents[pendingEventHead] = {
                static_cast<uint8_t>(mapping[pos]),
                static_cast<uint8_t>(switchState ? 1 : 0)};
            pendingEventHead = nextHead;
          }
        }
      }
    }
  }
}

void SwitchMatrix::handleEvent(Event* event) {
  switch (event->sourceId) {
    case EVENT_POLL_EVENTS: {
      while (true) {
        PendingMatrixSwitchEvent pending;
        const uint32_t irqState = save_and_disable_interrupts();
        if (pendingEventTail == pendingEventHead) {
          restore_interrupts(irqState);
          break;
        }
        pending = pendingEvents[pendingEventTail];
        pendingEventTail = static_cast<uint8_t>((pendingEventTail + 1) %
                                                MATRIX_SWITCH_EVENT_QUEUE_SIZE);
        restore_interrupts(irqState);

        _eventDispatcher->dispatch(new Event(EVENT_SOURCE_SWITCH,
                                             word(0, pending.number),
                                             pending.state));
      }
      break;
    }

    case EVENT_READ_SWITCHES:
      // The CPU requested all current states. Usually this event is sent when
      // the game gets started.
      if (active) {
        // Report the state each switch is actually in, the way Switches does.
        //
        // This used to send 0 for every mapped switch and leave the ON events
        // to the IRQ handler. The handler only fires on *change*, and by then
        // lastStable already matches reality, so nothing changed and no ON ever
        // followed: a closed switch stayed reported as open for the rest of the
        // session. A trough with balls sitting in it would read empty at game
        // start.
        //
        // Before the first scan lastStable holds the seeded idle pattern, so
        // this correctly reports all-open then too.
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
