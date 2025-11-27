/*
  SwitchMatrix.h.
  Created by Markus Kalkbrenner, 2022.

  Play more pinball!
*/

#ifndef Switches_h
#define Switches_h

#include <Arduino.h>

#include "../EventDispatcher/Event.h"
#include "../EventDispatcher/EventDispatcher.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#define SWITCHES_BASE_PIN 3
#define MAX_SWITCHES 16

class Switches : public EventListener {
 public:
  Switches(byte bId, EventDispatcher* eD) {
    boardId = bId;
    _eventDispatcher = eD;
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);
  }

  void setNumSwitches(uint8_t n) {
    numSwitches = n;
    validSwitchMask = (1u << numSwitches) - 1;
  }
  void registerSwitch(byte p, byte n, uint8_t debounceTimeMs);

  void handleEvent(Event* event);

  void handleEvent(ConfigEvent* event) {}

  void handleSwitchChanges(uint32_t raw);

  PIO pio = pio0;
  int sm = 2;  // State machine 0 and 1 are used by SwitchMatrix
  uint16_t validSwitchMask = (1u << MAX_SWITCHES) - 1;
  static Switches* instance;
  uint8_t numSwitches = MAX_SWITCHES;

  static void __not_in_flash_func(onSwitchChanges)() {
    // re-enable IRQ1 for next switch change (clear IRQ 1)
    pio0_hw->irq = 1u << 1;

    // Get 32 bit from FIFO
    uint32_t raw = pio_sm_get(instance->pio, instance->sm);
    instance->handleSwitchChanges((~raw) & instance->validSwitchMask);
  }

 private:
  byte boardId;

  bool running = false;
  bool active = false;

  byte port[MAX_SWITCHES] = {0};
  byte number[MAX_SWITCHES] = {0};
  uint8_t debounceSetting[MAX_SWITCHES] = {0};
  uint32_t debounceTime[MAX_SWITCHES] = {0};
  int last = -1;

  uint16_t currentStable = 0;

  EventDispatcher* _eventDispatcher;
};

#endif
