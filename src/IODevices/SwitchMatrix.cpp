#include "SwitchMatrix.h"

#include "SwitchMatrix.pio.h"

SwitchMatrix* SwitchMatrix::instance = nullptr;

void SwitchMatrix::setActiveLow() { activeLow = true; }

void SwitchMatrix::registerSwitch(byte p, byte n) {
  if (p < MATRIX_SWITCHES) {
    mapping[p] = n;
    active = true;
  }
}

void SwitchMatrix::handleRowChanges(uint32_t raw, uint8_t even) {
  absolute_time_t now = get_absolute_time();
  uint32_t changed = raw ^ lastStable[even];  // raw to raw comparison

  for (int column = 0; column < 4; column++) {
    for (int row = 0; row < 8; row++) {
      uint8_t pos = ((column * 2) + even) * 8 + row;
      if (mapping[pos] == 0) continue;  // Not registered

      uint32_t mask = 0x80000000 >> (column * 8 + row);

      if (changed & mask) {
        // Debounce
        if (absolute_time_diff_us(debounceTime[pos], now) >=
            MATRIX_SWITCH_DEBOUNCE * 1000) {
          debounceTime[pos] = now;

          // Convert RAW to logical pressed/released
          // -----------------------------------------
          // activeLow : pressed = raw_bit == 0
          // activeHigh: pressed = raw_bit == 1
          bool rawBit = (raw & mask) != 0;

          bool newState = activeLow ? (!rawBit)  // active-low: 0 = pressed
                                    : rawBit;    // active-high: 1 = pressed

          // Store the *raw* stable state
          if (rawBit)
            lastStable[even] |= mask;  // raw=1
          else
            lastStable[even] &= ~mask;  // raw=0

          // Dispatch all switch events as "local fast".
          // If a PWM output registered to it, we have "fast flip". Useful for
          // flippers, kick backs, jets and sling shots.
          _eventDispatcher->dispatch(new Event(
              EVENT_SOURCE_SWITCH, word(0, mapping[pos]), newState, true));
        }
      }
    }
  }
}

void SwitchMatrix::handleEvent(Event* event) {
  switch (event->sourceId) {
    case EVENT_READ_SWITCHES:
      // The CPU requested all current states. Usually this event is sent when
      // the game gets started.
      if (active) {
        // First, send OFF for all switches then ON for the active ones using
        // the IRQ handler.
        for (int i = 0; i < MATRIX_SWITCHES; i++) {
          if (mapping[i] != 0) {
            _eventDispatcher->dispatch(
                new Event(EVENT_SOURCE_SWITCH, word(0, mapping[i]), 0));
          }
        }

        if (!running) {
          instance = this;
          running = true;

          uint columns_offset;
          pio_sm_config c_columns_pio;
          uint odd_rows_offset;
          pio_sm_config c_odd_rows;
          uint even_rows_offset;
          pio_sm_config c_even_rows;

          if (activeLow) {
            extern const pio_program_t columns_active_low_pio_program;
            columns_offset =
                pio_add_program(pio, &columns_active_low_pio_program);
            c_columns_pio = columns_active_low_pio_program_get_default_config(
                columns_offset);

            extern const pio_program_t odd_rows_active_low_pio_program;
            uint odd_rows_offset =
                pio_add_program(pio, &odd_rows_active_low_pio_program);
            pio_sm_config c_odd_rows =
                odd_rows_active_low_pio_program_get_default_config(
                    odd_rows_offset);

            extern const pio_program_t even_rows_active_low_pio_program;
            uint even_rows_offset =
                pio_add_program(pio, &even_rows_active_low_pio_program);
            pio_sm_config c_even_rows =
                even_rows_active_low_pio_program_get_default_config(
                    even_rows_offset);
          } else {
            extern const pio_program_t columns_active_high_pio_program;
            columns_offset =
                pio_add_program(pio, &columns_active_high_pio_program);
            c_columns_pio = columns_active_high_pio_program_get_default_config(
                columns_offset);

            extern const pio_program_t odd_rows_active_high_pio_program;
            uint odd_rows_offset =
                pio_add_program(pio, &odd_rows_active_high_pio_program);
            pio_sm_config c_odd_rows =
                odd_rows_active_high_pio_program_get_default_config(
                    odd_rows_offset);

            extern const pio_program_t even_rows_active_high_pio_program;
            uint even_rows_offset =
                pio_add_program(pio, &even_rows_active_high_pio_program);
            pio_sm_config c_even_rows =
                even_rows_active_high_pio_program_get_default_config(
                    even_rows_offset);
          }

          // Columns
          sm_config_set_in_pins(&c_columns_pio, COLUMNS_BASE_PIN);
          // Connect 8 GPIOs to this PIO block
          for (uint i = 0; i < 8; i++) {
            pio_gpio_init(pio, COLUMNS_BASE_PIN + i);
          }
          // Set the pin direction at the PIO
          pio_sm_set_consecutive_pindirs(pio, sm_even_rows, COLUMNS_BASE_PIN, 8,
                                         true);
          sm_config_set_out_shift(&c_columns_pio, true, false, 8);
          pio_sm_init(pio, sm_columns, columns_offset, &c_columns_pio);
          pio_sm_set_enabled(pio, sm_columns, true);

          // Odd Rows
          sm_config_set_in_pins(&c_odd_rows, SWITCH_MATRIX_BASE_PIN);
          // Connect 16 GPIOs to this PIO block
          for (uint i = 0; i < 16; i++) {
            pio_gpio_init(pio, SWITCH_MATRIX_BASE_PIN + i);
          }
          // Set the pin direction at the PIO
          pio_sm_set_consecutive_pindirs(pio, sm_odd_rows,
                                         SWITCH_MATRIX_BASE_PIN, 16, false);
          sm_config_set_in_shift(&c_odd_rows, true, false, 8);
          pio_sm_init(pio, sm_odd_rows, odd_rows_offset, &c_odd_rows);
          irq_set_exclusive_handler(PIO0_IRQ_0, onOddRowChanges);
          irq_set_enabled(PIO0_IRQ_0, true);
          pio_set_irq0_source_enabled(pio, pis_interrupt0, true);
          pio_sm_set_enabled(pio, sm_odd_rows, true);

          // Even Rows
          sm_config_set_in_pins(&c_even_rows, SWITCH_MATRIX_BASE_PIN);
          // Connect 16 GPIOs to this PIO block
          for (uint i = 0; i < 16; i++) {
            pio_gpio_init(pio, SWITCH_MATRIX_BASE_PIN + i);
          }
          // Set the pin direction at the PIO
          pio_sm_set_consecutive_pindirs(pio, sm_even_rows,
                                         SWITCH_MATRIX_BASE_PIN, 16, false);
          sm_config_set_in_shift(&c_even_rows, true, false, 8);
          pio_sm_init(pio, sm_even_rows, even_rows_offset, &c_even_rows);
          irq_set_exclusive_handler(PIO0_IRQ_1, onEvenRowChanges);
          irq_set_enabled(PIO0_IRQ_1, true);
          pio_set_irq0_source_enabled(pio, pis_interrupt1, true);
          pio_sm_set_enabled(pio, sm_even_rows, true);
        }
      }
      break;
  }
}
