#ifndef HIGHPOWEROFFAWARE_h
#define HIGHPOWEROFFAWARE_h

#include <Arduino.h>

#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"

// The high-power gate as this board currently sees it, for the stats report.
//
// Diagnostics only. The host can see that a coil did not fire but not why: from
// its end, "the board refused because high power is off" and "the board never
// got the command" look the same. These bits say which.
//
//   bit 0  powerOn          the game-on solenoid is asserted
//   bit 1  coinDoorClosed   the coin door reads closed
//   bit 2  gameOnSolenoid   a game-on solenoid number was configured
//   bit 3  coinDoorSwitch   a coin door switch number was configured
inline volatile uint32_t g_highPowerGateBits = 0;

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
    tiltToggled = false;

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

    // Tilt is deliberately NOT part of powerOn: it must drop the flippers while
    // leaving the outhole kicker and trough eject alive, because those are what
    // get the balls back. A machine that killed all high power on tilt could
    // strand a ball with no way to recover it.
    if (tiltSwitch > 0 && event->sourceId == EVENT_SOURCE_SWITCH &&
        (byte)event->eventId == tiltSwitch) {
      const bool active = (bool)event->value;
      if (active != tiltActive) {
        tiltActive = active;
        tiltToggled = true;
      }
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
            // The switch NUMBER, not its state. This assigned to coinDoorClosed
            // instead, which left coinDoorSwitch at 0 - so the guard on it never
            // passed, the door never gated anything, and coinDoorClosed was
            // stuck true because a non-zero switch number is a true bool.
            //
            // Opening the coin door therefore did not cut high power, which is
            // the one thing this interlock exists to do.
            //
            // The host sends 0 when the door switch belongs to a board that is
            // not present, meaning "nobody can report this, do not gate on it".
            // EVENT_RUN turns that into coinDoorClosed = true below.
            coinDoorSwitch = event->value;
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

      case CONFIG_TOPIC_TILT_SWITCH:
        switch (event->key) {
          case CONFIG_TOPIC_NUMBER:
            tiltSwitch = event->value;
            break;
        }
        break;
    }
  }

  void resetHighPowerConfig() {
    coinDoorSwitch = 0;
    gameOnSolenoid = 0;
    tiltSwitch = 0;
    coinDoorClosed = false;
    powerOn = false;
    powerToggled = false;
    tiltActive = false;
    tiltToggled = false;
  }

  // High power in general: coils, flashers, lamps.
  bool highPowerAvailable() const { return powerOn && coinDoorClosed; }

  // Published by PwmDevices only. Several classes derive from this one, and a
  // single global would otherwise report whichever instance ran last rather
  // than the one that actually drives the coils.
  void publishGateBits() const {
    g_highPowerGateBits =
        static_cast<uint32_t>((powerOn ? 1 : 0) | (coinDoorClosed ? 2 : 0) |
                              (coinDoorSwitch > 0 ? 8 : 0)) |
        (static_cast<uint32_t>(gameOnSolenoid) << 8) |
        (static_cast<uint32_t>(coinDoorSwitch) << 16);
  }

  // Fast-flip outputs specifically: flippers, slingshots, pop bumpers. These
  // additionally require that the machine is not tilted.
  bool fastSwitchOutputsAllowed() const { return highPowerAvailable() && !tiltActive; }

 protected:
  byte coinDoorSwitch = 0;
  byte gameOnSolenoid = 0;
  byte tiltSwitch = 0;
  bool coinDoorClosed = false;
  bool powerOn = false;
  bool powerToggled = false;
  bool tiltActive = false;
  bool tiltToggled = false;
};

#endif
