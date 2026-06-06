#include "SwitchMatrix.h"

#include "SwitchMatrixPIO/ActiveHigh4Columns.pio.h"
#include "SwitchMatrixPIO/ActiveHigh4Rows.pio.h"
#include "SwitchMatrixPIO/ActiveHigh8Rows.pio.h"
#include "SwitchMatrixPIO/ActiveLow4Columns.pio.h"
#include "SwitchMatrixPIO/ActiveLow4Rows.pio.h"
#include "SwitchMatrixPIO/ActiveLow8Rows.pio.h"

SwitchMatrix* SwitchMatrix::instance = nullptr;

void SwitchMatrix::stopReader() {
  if (!running) {
    return;
  }

  pio_sm_set_enabled(pio, sm_columns, false);
  pio_sm_set_enabled(pio, sm_rows, false);
  pio_set_irq0_source_enabled(pio, pis_interrupt0, false);
  irq_set_enabled(PIO0_IRQ_0, false);
  if (instance == this) {
    instance = nullptr;
  }
  running = false;
}

void SwitchMatrix::startReader() {
  if (running) {
    return;
  }

  instance = this;
  running = true;

  uint columns_offset;
  pio_sm_config c_columns;
  uint rows_offset;
  pio_sm_config c_rows;

  if (activeLow) {
    extern const pio_program_t active_low_4_columns_pio_program;
    columns_offset = pio_add_program(pio, &active_low_4_columns_pio_program);
    c_columns =
        active_low_4_columns_pio_program_get_default_config(columns_offset);

    if (4 == numRows) {
      extern const pio_program_t active_low_4_rows_pio_program;
      rows_offset = pio_add_program(pio, &active_low_4_rows_pio_program);
      c_rows = active_low_4_rows_pio_program_get_default_config(rows_offset);
    } else {
      extern const pio_program_t active_low_8_rows_pio_program;
      rows_offset = pio_add_program(pio, &active_low_8_rows_pio_program);
      c_rows = active_low_8_rows_pio_program_get_default_config(rows_offset);
    }
  } else {
    extern const pio_program_t active_high_4_columns_pio_program;
    columns_offset = pio_add_program(pio, &active_high_4_columns_pio_program);
    c_columns =
        active_high_4_columns_pio_program_get_default_config(columns_offset);

    if (4 == numRows) {
      extern const pio_program_t active_high_4_rows_pio_program;
      rows_offset = pio_add_program(pio, &active_high_4_rows_pio_program);
      c_rows = active_high_4_rows_pio_program_get_default_config(rows_offset);
    } else {
      extern const pio_program_t active_high_8_rows_pio_program;
      rows_offset = pio_add_program(pio, &active_high_8_rows_pio_program);
      c_rows = active_high_8_rows_pio_program_get_default_config(rows_offset);
    }
  }

  columnsProgramLoaded = true;
  rowsProgramLoaded = true;
  columnsProgramOffset = columns_offset;
  rowsProgramOffset = rows_offset;
  loadedActiveLow = activeLow;
  loadedNumRows = numRows;

  sm_config_set_in_pins(&c_columns, COLUMNS_BASE_PIN);
  for (uint i = 0; i < NUM_COLUMNS; i++) {
    pio_gpio_init(pio, COLUMNS_BASE_PIN + i);
  }
  pio_sm_set_consecutive_pindirs(pio, sm_rows, COLUMNS_BASE_PIN, NUM_COLUMNS,
                                 true);
  sm_config_set_out_shift(&c_columns, false, false, 0);
  pio_sm_init(pio, sm_columns, columns_offset, &c_columns);
  pio_sm_set_enabled(pio, sm_columns, true);

  sm_config_set_in_pins(&c_rows, COLUMNS_BASE_PIN - numRows);
  for (uint i = 0; i < (numRows + NUM_COLUMNS); i++) {
    pio_gpio_init(pio, COLUMNS_BASE_PIN - numRows + i);
  }
  pio_sm_set_consecutive_pindirs(pio, sm_rows, COLUMNS_BASE_PIN - numRows,
                                 numRows + NUM_COLUMNS, false);
  sm_config_set_in_shift(&c_rows, false, false, 0);
  pio_sm_init(pio, sm_rows, rows_offset, &c_rows);
  irq_set_exclusive_handler(PIO0_IRQ_0, onRowChanges);
  irq_set_enabled(PIO0_IRQ_0, true);
  pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
  pio_sm_set_enabled(pio, sm_rows, true);
}

void SwitchMatrix::resendStableStates() {
  for (int column = 0; column < NUM_COLUMNS; column++) {
    for (int row = 0; row < numRows; row++) {
      const uint8_t pos = column * numRows + row;
      if (mapping[pos] == 0) {
        continue;
      }

      const uint32_t mask = 1u << ((NUM_COLUMNS - 1 - column) * numRows + row);
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

  if (columnsProgramLoaded) {
    if (loadedActiveLow) {
      extern const pio_program_t active_low_4_columns_pio_program;
      pio_remove_program(pio, &active_low_4_columns_pio_program,
                         columnsProgramOffset);
    } else {
      extern const pio_program_t active_high_4_columns_pio_program;
      pio_remove_program(pio, &active_high_4_columns_pio_program,
                         columnsProgramOffset);
    }
    columnsProgramLoaded = false;
  }

  if (rowsProgramLoaded) {
    if (loadedActiveLow) {
      if (loadedNumRows == 4) {
        extern const pio_program_t active_low_4_rows_pio_program;
        pio_remove_program(pio, &active_low_4_rows_pio_program,
                           rowsProgramOffset);
      } else {
        extern const pio_program_t active_low_8_rows_pio_program;
        pio_remove_program(pio, &active_low_8_rows_pio_program,
                           rowsProgramOffset);
      }
    } else {
      if (loadedNumRows == 4) {
        extern const pio_program_t active_high_4_rows_pio_program;
        pio_remove_program(pio, &active_high_4_rows_pio_program,
                           rowsProgramOffset);
      } else {
        extern const pio_program_t active_high_8_rows_pio_program;
        pio_remove_program(pio, &active_high_8_rows_pio_program,
                           rowsProgramOffset);
      }
    }
    rowsProgramLoaded = false;
  }

  activeLow = false;
  numRows = 4;
  loadedActiveLow = false;
  loadedNumRows = 4;
  active = false;
  memset(mapping, 0, sizeof(mapping));
  lastStable = 0;
  pendingEventHead = 0;
  pendingEventTail = 0;
  memset(pendingEvents, 0, sizeof(pendingEvents));
  memset(debounceTime, 0, sizeof(debounceTime));
}

void SwitchMatrix::registerSwitch(byte p, byte n) {
  if (p < (NUM_COLUMNS * numRows)) {
    mapping[p] = n;
    active = true;
  }
}

void SwitchMatrix::handleRowChanges(uint32_t raw) {
  absolute_time_t now = get_absolute_time();
  uint32_t changed = raw ^ lastStable;  // raw to raw comparison

  for (int column = 0; column < NUM_COLUMNS; column++) {
    for (int row = 0; row < numRows; row++) {
      uint8_t pos = column * numRows + row;
      if (mapping[pos] == 0) continue;  // Not registered

      uint32_t mask = 1u << ((NUM_COLUMNS - 1 - column) * numRows + row);

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
        // First, send OFF for all switches then ON for the active ones using
        // the IRQ handler.
        for (int i = 0; i < (NUM_COLUMNS * numRows); i++) {
          if (mapping[i] != 0) {
            _eventDispatcher->dispatch(
                new Event(EVENT_SOURCE_SWITCH, word(0, mapping[i]), 0));
          }
        }

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
