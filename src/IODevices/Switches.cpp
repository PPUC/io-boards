#include "Switches.h"

#include "SwitchesPIO/ActiveLow16Switches.pio.h"
#include "SwitchesPIO/ActiveLow4Switches.pio.h"
#include "SwitchesPIO/ActiveLow8Switches.pio.h"

Switches* Switches::instance = nullptr;

void Switches::stopReader() {
  if (!running) {
    return;
  }

  pio_sm_set_enabled(pio, sm, false);
  pio_set_irq1_source_enabled(pio, pis_interrupt1, false);
  irq_set_enabled(PIO0_IRQ_1, false);
  if (instance == this) {
    instance = nullptr;
  }
  running = false;
}

void Switches::resetConfig() {
  stopReader();

  if (programLoaded) {
    switch (loadedNumSwitches) {
      case 4: {
        extern const pio_program_t active_low_4_switches_pio_program;
        pio_remove_program(pio, &active_low_4_switches_pio_program,
                           programOffset);
        break;
      }
      case 8: {
        extern const pio_program_t active_low_8_switches_pio_program;
        pio_remove_program(pio, &active_low_8_switches_pio_program,
                           programOffset);
        break;
      }
      case MAX_SWITCHES:
      default: {
        extern const pio_program_t active_low_16_switches_pio_program;
        pio_remove_program(pio, &active_low_16_switches_pio_program,
                           programOffset);
        break;
      }
    }
    programLoaded = false;
  }

  active = false;
  last = -1;
  numSwitches = MAX_SWITCHES;
  loadedNumSwitches = MAX_SWITCHES;
  validSwitchMask = (1u << MAX_SWITCHES) - 1;
  currentStable = 0;
  pendingEventHead = 0;
  pendingEventTail = 0;
  memset(pendingEvents, 0, sizeof(pendingEvents));
  memset(port, 0, sizeof(port));
  memset(number, 0, sizeof(number));
  memset(debounceSetting, 0, sizeof(debounceSetting));
  memset(debounceMode, SWITCH_DEBOUNCE_STANDARD, sizeof(debounceMode));
  memset(debounceTimeUs, 0, sizeof(debounceTimeUs));
  memset(pendingDebounceSinceUs, 0, sizeof(pendingDebounceSinceUs));
  pioBaseline = 0;
  latestRaw = 0;
  pendingDebounceMask = 0;
  pendingDebounceState = 0;
  memset(localFastSwitch, 0, sizeof(localFastSwitch));
}

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

void Switches::setDebounceMode(byte n, uint8_t mode) {
  if (mode > SWITCH_DEBOUNCE_SLOW_STABLE) {
    mode = SWITCH_DEBOUNCE_STANDARD;
  }

  for (int i = 0; i <= last; i++) {
    if (number[i] == n) {
      debounceMode[i] = mode;
      return;
    }
  }
}

void Switches::markLocalFastSwitch(byte n) {
  localFastSwitch[n] = true;
}

uint32_t Switches::debounceWindowUs(uint8_t index) const {
  uint32_t windowUs = static_cast<uint32_t>(debounceSetting[index]) * 1000u;
  if (debounceMode[index] == SWITCH_DEBOUNCE_SLOW_STABLE) {
    windowUs *= 4u;
  }
  return windowUs;
}

void Switches::enqueuePendingSwitchEvent(uint8_t switchNumber, uint8_t state) {
  const uint8_t nextHead =
      static_cast<uint8_t>((pendingEventHead + 1) % SWITCH_EVENT_QUEUE_SIZE);
  if (nextHead == pendingEventTail) {
    pendingEventTail =
        static_cast<uint8_t>((pendingEventTail + 1) % SWITCH_EVENT_QUEUE_SIZE);
  }

  pendingEvents[pendingEventHead] = {switchNumber, state};
  pendingEventHead = nextHead;
}

void Switches::acceptSwitchState(uint8_t index, uint32_t mask, bool switchState,
                                 uint32_t nowUs) {
  debounceTimeUs[index][switchState ? 1 : 0] = nowUs;
  if (switchState)
    currentStable |= mask;
  else
    currentStable &= ~mask;
  pendingDebounceMask &= ~mask;
  pendingDebounceSinceUs[index] = 0;
  enqueuePendingSwitchEvent(static_cast<uint8_t>(number[index]),
                            static_cast<uint8_t>(switchState ? 1 : 0));
}

void Switches::deferSwitchState(uint8_t index, uint32_t mask, bool switchState,
                                uint32_t nowUs) {
  if (((pendingDebounceMask & mask) == 0) ||
      (((pendingDebounceState & mask) != 0) != switchState)) {
    pendingDebounceSinceUs[index] = nowUs;
  }

  if (switchState)
    pendingDebounceState |= mask;
  else
    pendingDebounceState &= ~mask;
  pendingDebounceMask |= mask;
}

void Switches::flushPendingDebounce(uint32_t nowUs) {
  if (pendingDebounceMask == 0) {
    return;
  }

  for (int i = 0; i <= last; i++) {
    uint32_t mask = 1u << (port[i] - SWITCHES_BASE_PIN);
    if ((pendingDebounceMask & mask) == 0) {
      continue;
    }

    const bool switchState = (pendingDebounceState & mask) != 0;
    if (((latestRaw & mask) != 0) != switchState) {
      pendingDebounceMask &= ~mask;
      continue;
    }
    if (((currentStable & mask) != 0) == switchState) {
      pendingDebounceMask &= ~mask;
      continue;
    }

    const uint32_t debounceUs =
        debounceWindowUs(static_cast<uint8_t>(i));
    const uint32_t lastAcceptedUs = debounceTimeUs[i][switchState ? 1 : 0];
    const uint32_t sinceUs = pendingDebounceSinceUs[i];
    const bool stableLongEnough =
        debounceMode[i] == SWITCH_DEBOUNCE_STANDARD ||
        debounceMode[i] == SWITCH_DEBOUNCE_SLOW_STABLE;
    if (stableLongEnough && sinceUs != 0 && (nowUs - sinceUs) < debounceUs) {
      continue;
    }
    if (!stableLongEnough && lastAcceptedUs != 0 &&
        (nowUs - lastAcceptedUs) < debounceUs) {
      continue;
    }

    acceptSwitchState(static_cast<uint8_t>(i), mask, switchState, nowUs);
  }
}

void Switches::handleSwitchChanges(uint32_t raw) {
  uint32_t nowUs = micros();
  // A pending edge may have already survived its debounce window before this
  // new raw transition arrived. Commit it against the previous raw sample
  // first so a valid quick press/release cannot be canceled by the release IRQ
  // before the normal poll loop gets a chance to flush it.
  flushPendingDebounce(nowUs);
  latestRaw = static_cast<uint16_t>(raw);
  uint32_t changed = raw ^ currentStable;
  if (changed > 0) {
    uint32_t allSwitchesMask = 0;

    for (int i = 0; i <= last; i++) {
      uint32_t mask = 1u << (port[i] - SWITCHES_BASE_PIN);
      allSwitchesMask |= mask;

      if (changed & mask) {
        bool switchState = ((raw & mask) != 0);
        const uint32_t debounceUs = debounceWindowUs(static_cast<uint8_t>(i));
        const uint32_t lastAcceptedUs = debounceTimeUs[i][switchState ? 1 : 0];
        switch (debounceMode[i]) {
          case SWITCH_DEBOUNCE_FAST_FLIP:
            if (switchState &&
                (lastAcceptedUs == 0 ||
                 (nowUs - lastAcceptedUs) >= debounceUs)) {
              // Flipper button close must be as fast as possible. Opening is
              // still debounced below so contact bounce right after a press
              // cannot drop the flipper again.
              acceptSwitchState(static_cast<uint8_t>(i), mask, switchState,
                                nowUs);
            } else {
              deferSwitchState(static_cast<uint8_t>(i), mask, switchState,
                               nowUs);
            }
            break;

          case SWITCH_DEBOUNCE_SLOW_STABLE:
          case SWITCH_DEBOUNCE_STANDARD:
          default:
            deferSwitchState(static_cast<uint8_t>(i), mask, switchState,
                             nowUs);
            break;
        }
      }
    }

    // Set unregistered switches to raw value to for next comparison
    currentStable =
        (currentStable & allSwitchesMask) + (raw & ~allSwitchesMask);
    pioBaseline = (pioBaseline & allSwitchesMask) + (raw & ~allSwitchesMask);
  }

  // Push the observed state to PIO for next detection. Reported stable state is
  // tracked separately so a temporarily suppressed edge can be committed later
  // without causing repeated PIO IRQs while the input is physically steady.
  pioBaseline = static_cast<uint16_t>(raw);
  pio_sm_put(pio, sm, ~pioBaseline);
}

void Switches::handleEvent(Event* event) {
  switch (event->sourceId) {
    case EVENT_POLL_EVENTS: {
      const uint32_t irqState = save_and_disable_interrupts();
      flushPendingDebounce(micros());
      restore_interrupts(irqState);

      while (true) {
        PendingSwitchEvent pending;
        const uint32_t irqState = save_and_disable_interrupts();
        if (pendingEventTail == pendingEventHead) {
          restore_interrupts(irqState);
          break;
        }
        pending = pendingEvents[pendingEventTail];
        pendingEventTail = static_cast<uint8_t>((pendingEventTail + 1) %
                                                SWITCH_EVENT_QUEUE_SIZE);
        restore_interrupts(irqState);

        _eventDispatcher->dispatch(new Event(EVENT_SOURCE_SWITCH,
                                             word(0, pending.number),
                                             pending.state,
                                             localFastSwitch[pending.number]));
      }
      break;
    }

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
          programLoaded = true;
          programOffset = offset;
          loadedNumSwitches = numSwitches;

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

    case EVENT_REFRESH_SWITCHES:
      if (active) {
        for (int i = 0; i <= last; i++) {
          const bool switchState = digitalRead(port[i]) == LOW;
          _eventDispatcher->refreshDedicatedSwitchState(number[i], switchState);
        }
        _eventDispatcher->clearDedicatedSwitchReportHistory();
      }
      break;
  }
}
