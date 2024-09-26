#include "PwmDevices.h"

#include "EventDispatcher/CrossLinkDebugger.h"

void PwmDevices::registerSolenoid(byte p, byte n, byte pow, uint16_t minPT,
                                  uint16_t maxPT, byte hP, uint16_t hPAT,
                                  byte fS) {
  if (last < MAX_PWM_OUTPUTS) {
    port[last] = p;
    number[last] = n;
    power[last] = pow;
    minPulseTime[last] = minPT;
    maxPulseTime[last] = maxPT;
    holdPower[last] = hP;
    holdPowerActivationTime[last] = hPAT;
    fastSwitch[last] = fS;

    pinMode(p, OUTPUT);
    digitalWrite(p, 0);
    type[last++] = PWM_TYPE_SOLENOID;
  }
}

void PwmDevices::registerFlasher(byte p, byte n, byte pow) {
  if (last < MAX_PWM_OUTPUTS) {
    port[last] = p;
    number[last] = n;
    power[last] = pow;

    pinMode(p, OUTPUT);
    analogWrite(p, 0);
    type[last++] = PWM_TYPE_FLASHER;
  }
}

void PwmDevices::registerLamp(byte p, byte n, byte pow) {
  if (last < MAX_PWM_OUTPUTS) {
    port[last] = p;
    number[last] = n;
    power[last] = pow;

    pinMode(p, OUTPUT);
    analogWrite(p, 0);
    type[last++] = PWM_TYPE_LAMP;
  }
}

void PwmDevices::reset() {
  for (uint8_t i = 0; i < MAX_PWM_OUTPUTS; i++) {
    if (i < last) {
      // Turn off PWM output.
      digitalWrite(port[i], 0);
    }
    port[i] = 0;
    number[i] = 0;
    power[i] = 0;
    minPulseTime[i] = 0;
    maxPulseTime[i] = 0;
    holdPower[i] = 0;
    holdPowerActivationTime[i] = 0;
    fastSwitch[i] = 0;
    type[i] = 0;
    activated[i] = 0;
    scheduled[i] = 0;
  }

  last = 0;
}

void PwmDevices::update() {
  _ms = millis();

  // Iterate over all outputs.
  for (byte i = 0; i < last; i++) {
    if (activated[i] > 0) {
      // The output is active.
      uint16_t timePassed = _ms - activated[i];
      if ((scheduled[i] && (minPulseTime[i] > 0) &&
           (timePassed > minPulseTime[i])) ||
          ((maxPulseTime[i] > 0) && (timePassed > maxPulseTime[i]))) {
        // Deactivate the output if it is scheduled for delayed deactivation and
        // the minimum pulse time is reached. Deactivate the output if the
        // maximum pulse time is reached.
        digitalWrite(port[i], 0);
        activated[i] = 0;
        scheduled[i] = false;
        CrossLinkDebugger::debug(
            "Performed scheduled deactivation of PWM device on port %d after "
            "%dms",
            port[i], timePassed);
      } else if ((holdPowerActivationTime[i] > 0) &&
                 (timePassed > holdPowerActivationTime[i])) {
        // Reduce the power of the activated output if the hold power activation
        // time pased since the activation.
        analogWrite(port[i], holdPower[i]);
        CrossLinkDebugger::debug(
            "Reduced power of PWM device on port %d after %dms", port[i],
            timePassed);
      }
    }
  }
}

void PwmDevices::updateSolenoidOrFlasher(bool targetState, byte i) {
  _ms = millis();

  if (targetState && activated[i] == 0) {
    // Event received to activate the output and output isn't activated already.
    // Activate it!
    analogWrite(port[i], power[i]);
    // Rememebr when it got activated.
    activated[i] = _ms;
    CrossLinkDebugger::debug("Activated PWM device on port %d", port[i]);
  } else if (!targetState && activated[i] > 0) {
    // Event received to deactivate the output.
    // Check if a minimum pulse time is configured for this output.
    if ((_ms >= activated[i]) && (minPulseTime[i] > 0) &&
        (_ms - activated[i]) < minPulseTime[i]) {
      // A minimum pulse time is configured for this output.
      // Don't deactivate it immediately but schedule its later deactivation.
      scheduled[i] = true;
    } else {
      // Deactivate the output.
      digitalWrite(port[i], 0);
      // Mark the output as deactivated.
      activated[i] = 0;
      CrossLinkDebugger::debug("Deactivated PWM device on port %d", port[i]);
    }
  }
}

void PwmDevices::handleEvent(Event *event) {
  HighPowerOffAware::handleEvent(event);

  _ms = millis();

  if (powerOn && coinDoorClosed) {
    switch (event->sourceId) {
      case EVENT_SOURCE_SOLENOID:
        for (byte i = 0; i < last; i++) {
          if ((type[i] == PWM_TYPE_SOLENOID || type[i] == PWM_TYPE_FLASHER) &&
              number[i] == (byte)event->eventId) {
            updateSolenoidOrFlasher((bool)event->value, i);
          }
        }
        break;

      case EVENT_SOURCE_SWITCH:
        // A switch event was triggered or received. Activate or deactivate any
        // output that is configured as "fastSwitch" for that switch.
        for (byte i = 0; i < last; i++) {
          if (type[i] == PWM_TYPE_SOLENOID &&
              fastSwitch[i] == (byte)event->eventId) {
            updateSolenoidOrFlasher((bool)event->value, i);
          }
        }
        break;

      case EVENT_SOURCE_LIGHT:
        for (byte i = 0; i < last; i++) {
          if (type[i] == PWM_TYPE_LAMP && number[i] == (byte)event->eventId) {
            if (event->value) {
              analogWrite(port[i], power[i]);
            } else if (activated[i]) {
              analogWrite(port[i], 0);
            }
          }
        }
        break;
    }
  } else if (powerToggled) {
    for (byte i = 0; i < last; i++) {
      // Deactivate the output.
      digitalWrite(port[i], 0);
      // Mark the output as deactivated.
      activated[i] = 0;
      powerOn = false;
      CrossLinkDebugger::debug("Deactivated PWM device on port %d", port[i]);
    }
  }
}