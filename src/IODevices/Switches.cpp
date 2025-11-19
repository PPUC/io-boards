#include "Switches.h"

#include "Switches.pio.h"

Switches* Switches::instance = nullptr;

void Switches::registerSwitch(byte p, byte n) {
  if (last < (MAX_SWITCHES - 1)) {
    port[++last] = p;
    number[last] = n;
    active = true;
  }
}

void Switches::handleSwitchChanges(uint16_t raw) {
  absolute_time_t now = get_absolute_time();
  uint16_t changed = raw ^ lastStable;  // raw to raw comparison

  for (int i = 0; i < MAX_SWITCHES; i++) {
    if (number[i] == 0) continue;  // Not registered

    uint16_t mask = 1u << i;

    if (changed & mask) {
      // Debounce
      if (absolute_time_diff_us(debounceTime[i], now) >=
          SWITCH_DEBOUNCE * 1000) {
        debounceTime[i] = now;

        bool rawBit = (raw & mask) != 0;

        // logical pressed state for active-low
        bool newState = !rawBit;

        // store raw stable bit (not logical)
        if (rawBit)
          lastStable |= mask;  // raw = 1
        else
          lastStable &= ~mask;  // raw = 0

        // Dispatch all switch events as "local fast".
        // If a PWM output registered to it, we have "fast flip". Useful for
        // flippers, kick backs, jets and sling shots.
        _eventDispatcher->dispatch(
            new Event(EVENT_SOURCE_SWITCH, word(0, number[i]), newState, true));
      }
    }
  }
}

void Switches::handleEvent(Event* event) {
  switch (event->sourceId) {
    case EVENT_READ_SWITCHES:
      // The CPU requested all current states. Usually this event is sent when
      // the game gets started.
      if (active) {
        // First, send OFF for all switches then ON for the active ones using
        // the IRQ handler.
        for (int i = 0; i <= last; i++) {
          if (number[i] != 0) {
            _eventDispatcher->dispatch(
                new Event(EVENT_SOURCE_SWITCH, word(0, number[i]), 0));
          }
        }

        if (!running) {
          instance = this;
          running = true;

          extern const pio_program_t switches_pio_program;
          uint offset = pio_add_program(pio, &switches_pio_program);
          pio_sm_config c = switches_pio_program_get_default_config(offset);

          sm_config_set_in_pins(&c, SWITCHES_BASE_PIN);
          sm_config_set_sideset_pins(&c, 15);  // Side-set begins at GPIO 15
          sm_config_set_set_pins(&c, 15, 4);   // Set begins at GPIO 15

          // Connect 16 GPIOs to this PIO block
          for (uint i = 0; i < 16; i++) {
            pio_gpio_init(pio, SWITCHES_BASE_PIN + i);
          }

          // Set the pin direction at the PIO
          pio_sm_set_consecutive_pindirs(pio, sm, SWITCHES_BASE_PIN, 16, false);

          sm_config_set_in_shift(&c, true, false, 16);

          pio_sm_init(pio, sm, offset, &c);

          irq_set_exclusive_handler(PIO0_IRQ_0, onSwitchChanges);
          irq_set_enabled(PIO0_IRQ_0, true);
          pio_set_irq0_source_enabled(pio, pis_interrupt0, true);

          pio_sm_set_enabled(pio, sm, true);
        }
      }
      break;
  }
}
