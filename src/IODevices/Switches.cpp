#include "Switches.h"

#include "SwitchesPIO/ActiveLow16Switches.pio.h"
#include "SwitchesPIO/ActiveLow4Switches.pio.h"
#include "SwitchesPIO/ActiveLow8Switches.pio.h"

Switches* Switches::instance = nullptr;

void Switches::registerSwitch(byte p, byte n, uint8_t debounceTimeMs) {
  const int pinIndex = static_cast<int>(p) - SWITCHES_BASE_PIN;
  if (pinIndex < 0 || pinIndex >= numSwitches) {
    return;
  }

  int index = findRegisteredSwitch(p, n);
  if (index < 0) {
    if (last >= (numSwitches - 1)) return;
    index = ++last;
  }
  port[index] = p;
  number[index] = n;
  debounceSetting[index] = debounceTimeMs;
  active = true;
}

void Switches::handleSwitchChanges(uint32_t raw) {
  uint32_t now = millis();
  uint32_t changed = raw ^ currentStable;
  if (changed > 0) {
    uint32_t allSwitchesMask = 0;

    for (int i = 0; i <= last; i++) {
      uint32_t mask = 1u << (port[i] - SWITCHES_BASE_PIN);
      allSwitchesMask |= mask;

      if (changed & mask) {
        // Debounce
        if ((debounceTime[i] + debounceSetting[i]) < now) {
          debounceTime[i] = now;
          bool switchState = ((raw & mask) != 0);
          if (switchState)
            currentStable |= mask;  // set bit in lastStable to 1
          else
            currentStable &= ~mask;  // set bit in lastStable to 0
          // Dispatch all switch events as "local fast".
          // If a PWM output registered to it, we have "fast flip". Useful for
          // flippers, kick backs, jets and sling shots.
          _eventDispatcher->dispatch(new Event(EVENT_SOURCE_SWITCH,
                                               word(0, number[i]),
                                               switchState ? 1 : 0, true));
          // digitalWrite(LED_BUILTIN, switchState);
        }
      }
    }

    // Set unregistered switches to raw value to for next comparison
    currentStable =
        (currentStable & allSwitchesMask) + (raw & ~allSwitchesMask);
  }

  // Push debounced state to PIO for next detection
  pio_sm_put(pio, sm, ~currentStable);
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
            uint16_t mask = 1u << (port[i] - SWITCHES_BASE_PIN);
            _eventDispatcher->dispatch(
                new Event(EVENT_SOURCE_SWITCH, word(0, number[i]),
                          ((currentStable & mask) > 0) ? 1 : 0));
          }
        }

        if (!running) {
          instance = this;
          running = true;

          uint offset;
          pio_sm_config c;

          switch (numSwitches) {
            case 4:
              extern const pio_program_t active_low_4_switches_pio_program;
              offset = pio_add_program(pio, &active_low_4_switches_pio_program);
              c = active_low_4_switches_pio_program_get_default_config(offset);
              break;

            case 8:
              extern const pio_program_t active_low_8_switches_pio_program;
              offset = pio_add_program(pio, &active_low_8_switches_pio_program);
              c = active_low_8_switches_pio_program_get_default_config(offset);
              break;

            case MAX_SWITCHES:
            default:
              extern const pio_program_t active_low_16_switches_pio_program;
              offset =
                  pio_add_program(pio, &active_low_16_switches_pio_program);
              c = active_low_16_switches_pio_program_get_default_config(offset);
              break;
          }

          sm_config_set_in_pins(&c, SWITCHES_BASE_PIN);
          if (MAX_SWITCHES == numSwitches) {
            // Using GPIO 15-18 as switch inputs on IO_16_8_1 board requires
            // resetting the sateful input after reading.
            // Set begins at GPIO 15 for 4 pins.
            sm_config_set_set_pins(&c, 15, 4);
            // Side-set begins at GPIO 15.
            sm_config_set_sideset_pins(&c, 15);
          }
          // Connect GPIOs to this PIO block
          for (uint i = 0; i < numSwitches; i++) {
            pio_gpio_init(pio, SWITCHES_BASE_PIN + i);
          }
          // Set the pin direction at the PIO
          pio_sm_set_consecutive_pindirs(pio, sm, SWITCHES_BASE_PIN,
                                         numSwitches, false);
          sm_config_set_in_shift(&c, false, false, 0);
          pio_sm_init(pio, sm, offset, &c);
          irq_set_exclusive_handler(PIO0_IRQ_1, onSwitchChanges);
          irq_set_enabled(PIO0_IRQ_1, true);
          pio_set_irq1_source_enabled(pio, pis_interrupt1, true);
          pio_sm_set_enabled(pio, sm, true);
        }
      }
      break;
  }
}
