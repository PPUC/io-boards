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
#include "hardware/sync.h"

#define SWITCHES_BASE_PIN 3
#define MAX_SWITCHES 16
#define MAX_LOCAL_FAST_SWITCH_NUMBER 255
#define SWITCH_EVENT_QUEUE_SIZE 32

struct PendingSwitchEvent {
  uint8_t number;
  uint8_t state;
};

class Switches : public EventListener {
 public:
  Switches(byte bId, EventDispatcher* eD) {
    boardId = bId;
    _eventDispatcher = eD;
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);
    _eventDispatcher->addListener(this, EVENT_REFRESH_SWITCHES);
  }

  void setNumSwitches(uint8_t n) {
    numSwitches = n;
    validSwitchMask = (1u << numSwitches) - 1;
  }
  void registerSwitch(byte p, byte n, uint8_t debounceTimeMs);
  void setDebounceMode(byte n, uint8_t mode);
  void markLocalFastSwitch(byte n);
  void resetConfig();

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
  void stopReader();
  bool startReader();
  bool refreshDedicatedSwitchStates();
  void acceptSwitchState(uint8_t index, uint32_t mask, bool switchState,
                         uint32_t nowUs);
  void deferSwitchState(uint8_t index, uint32_t mask, bool switchState,
                        uint32_t nowUs);
  void enqueuePendingSwitchEvent(uint8_t switchNumber, uint8_t state);
  void flushPendingDebounce(uint32_t nowUs);
  uint32_t debounceWindowUs(uint8_t index) const;
  int findRegisteredSwitch(byte p, byte n) const {
    for (int i = 0; i <= last; i++) {
      if (port[i] == p || number[i] == n) {
        return i;
      }
    }
    return -1;
  }

  byte boardId;

  bool running = false;
  bool active = false;
  bool programLoaded = false;
  uint programOffset = 0;
  uint8_t loadedNumSwitches = MAX_SWITCHES;

  byte port[MAX_SWITCHES] = {0};
  byte number[MAX_SWITCHES] = {0};
  uint8_t debounceSetting[MAX_SWITCHES] = {0};
  uint8_t debounceMode[MAX_SWITCHES] = {SWITCH_DEBOUNCE_STANDARD};
  uint32_t debounceTimeUs[MAX_SWITCHES][2] = {{0}};
  uint32_t pendingDebounceSinceUs[MAX_SWITCHES] = {0};
  int last = -1;

  uint16_t currentStable = 0;
  uint16_t pioBaseline = 0;
  uint16_t latestRaw = 0;
  uint16_t pendingDebounceMask = 0;
  uint16_t pendingDebounceState = 0;
  volatile uint8_t pendingEventHead = 0;
  volatile uint8_t pendingEventTail = 0;
  PendingSwitchEvent pendingEvents[SWITCH_EVENT_QUEUE_SIZE] = {};
  volatile bool dedicatedSwitchStateChangedSinceCorrection = false;
  bool localFastSwitch[MAX_LOCAL_FAST_SWITCH_NUMBER + 1] = {false};

  EventDispatcher* _eventDispatcher;
};

#endif
