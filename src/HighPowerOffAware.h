#ifndef HIGHPOWEROFFAWARE_h
#define HIGHPOWEROFFAWARE_h

#include <Arduino.h>

#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"

class HighPowerOffAware : public EventListener {
 public:
  HighPowerOffAware() {}

  HighPowerOffAware(EventDispatcher *eventDispatcher) {
    eventDispatcher->addListener(this, EVENT_CONFIGURATION);
    eventDispatcher->addListener(this, EVENT_RUN);
    eventDispatcher->addListener(this, EVENT_SOURCE_SOLENOID);
    eventDispatcher->addListener(this, EVENT_SOURCE_SWITCH);
  }

  void handleEvent(Event *event) {
    powerToggled = false;

    if (coinDoorSwitch > 0 && event->sourceId == EVENT_SOURCE_SWITCH &&
        (byte)event->eventId == coinDoorSwitch) {
      coinDoorClosed = (bool)event->value;
      powerToggled = true;
    }

    if (gameOnSolenoid > 0 && event->sourceId == EVENT_SOURCE_SOLENOID &&
        (byte)event->eventId == gameOnSolenoid) {
      powerOn = (bool)event->value;
      powerToggled = true;
    }

    if (event->sourceId == EVENT_RUN) {
      // Fake coin door switch and gome on solenoid if not present in the
      // current game.
      if ((bool)event->value) {
        if (0 == coinDoorSwitch) coinDoorClosed = true;
        if (0 == gameOnSolenoid) powerOn = true;
      } else {
        powerOn = false;
        powerToggled = true;
      }
    }
  }

  void handleEvent(ConfigEvent *event) {
    switch (event->topic) {
      case CONFIG_TOPIC_COIN_DOOR_CLOSED_SWITCH:
        switch (event->key) {
          case CONFIG_TOPIC_NUMBER:
            coinDoorClosed = event->value;
            break;
        }
        break;

      case CONFIG_TOPIC_GAME_ON_SOLENOID:
        switch (event->key) {
          case CONFIG_TOPIC_NUMBER:
            gameOnSolenoid = event->value;
            break;
        }
        break;
    }
  }

 protected:
  byte coinDoorSwitch = 0;
  byte gameOnSolenoid = 0;
  bool coinDoorClosed = false;
  bool powerOn = false;
  bool powerToggled = false;
};

#endif
