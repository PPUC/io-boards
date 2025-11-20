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
#define SWITCH_DEBOUNCE 20

class Switches : public EventListener {
 public:
  Switches(byte bId, EventDispatcher* eD) {
    boardId = bId;
    _eventDispatcher = eD;
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);
  }

  void setNumSwitches(uint8_t n) { numSwitches = n; }
  void registerSwitch(byte p, byte n);

  void handleEvent(Event* event);

  void handleEvent(ConfigEvent* event) {}

  void handleSwitchChanges(uint16_t raw);

  PIO pio = pio0;
  int sm = 2; // State machine 0 and 1 are used by SwitchMatrix

  static Switches* instance;

  static void __not_in_flash_func(onSwitchChanges)() {
    // IRQ1 clear
    pio0_hw->irq = 1u << 1;

    // Get 16 bit from FIFO
    uint32_t raw = pio_sm_get_blocking(instance->pio, instance->sm);
    instance->handleSwitchChanges(raw & 0xFFFF);
  }

 private:
  byte boardId;
  uint8_t numSwitches = MAX_SWITCHES;
  bool running = false;
  bool active = false;

  byte port[MAX_SWITCHES] = {0};
  byte number[MAX_SWITCHES] = {0};
  int last = -1;

  uint16_t lastStable = 0;
  absolute_time_t debounceTime[MAX_SWITCHES][2] = {0};

  EventDispatcher* _eventDispatcher;
};

#endif
