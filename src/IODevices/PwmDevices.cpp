#include "PwmDevices.h"

#include "EventDispatcher/CrossLinkDebugger.h"

int PwmDevices::findRegisteredOutput(byte outputType, byte p, byte n) const {
  for (byte i = 0; i < last; i++) {
    if (type[i] != outputType) continue;
    if (port[i] == p || number[i] == n) {
      return i;
    }
  }
  return -1;
}

void PwmDevices::registerSolenoid(byte p, byte n, byte pow, uint16_t minPT,
                                  uint16_t maxPT, byte hP, uint16_t hPAT,
                                  byte fS) {
  int index = findRegisteredOutput(PWM_TYPE_SOLENOID, p, n);
  if (index < 0) {
    if (last >= MAX_PWM_OUTPUTS) return;
    index = last++;
  }

  port[index] = p;
  number[index] = n;
  power[index] = pow;
  minPulseTime[index] = minPT;
  maxPulseTime[index] = maxPT;
  holdPower[index] = hP;
  holdPowerActivationTime[index] = hPAT;
  fastSwitch[index] = fS;
  type[index] = PWM_TYPE_SOLENOID;
  activated[index] = 0;
  currentPower[index] = 0;
  scheduled[index] = false;
  fastSwitchClosed[index] = false;
  fastSwitchManagedActive[index] = false;
  fastSwitchWaitForRelease[index] = false;

  pinMode(p, OUTPUT);
  analogWrite(p, 0);
}

void PwmDevices::registerFlasher(byte p, byte n, byte pow) {
  int index = findRegisteredOutput(PWM_TYPE_FLASHER, p, n);
  if (index < 0) {
    if (last >= MAX_PWM_OUTPUTS) return;
    index = last++;
  }

  port[index] = p;
  number[index] = n;
  power[index] = pow;
  minPulseTime[index] = 0;
  maxPulseTime[index] = 0;
  holdPower[index] = 0;
  holdPowerActivationTime[index] = 0;
  fastSwitch[index] = 0;
  type[index] = PWM_TYPE_FLASHER;
  activated[index] = 0;
  currentPower[index] = 0;
  scheduled[index] = false;
  fastSwitchClosed[index] = false;
  fastSwitchManagedActive[index] = false;
  fastSwitchWaitForRelease[index] = false;

  pinMode(p, OUTPUT);
  analogWrite(p, 0);
}

void PwmDevices::registerLamp(byte p, byte n, byte pow) {
  int index = findRegisteredOutput(PWM_TYPE_LAMP, p, n);
  if (index < 0) {
    if (last >= MAX_PWM_OUTPUTS) return;
    index = last++;
  }

  port[index] = p;
  number[index] = n;
  power[index] = pow;
  minPulseTime[index] = 0;
  maxPulseTime[index] = 0;
  holdPower[index] = 0;
  holdPowerActivationTime[index] = 0;
  fastSwitch[index] = 0;
  type[index] = PWM_TYPE_LAMP;
  activated[index] = 0;
  currentPower[index] = 0;
  scheduled[index] = false;
  fastSwitchClosed[index] = false;
  fastSwitchManagedActive[index] = false;
  fastSwitchWaitForRelease[index] = false;

  pinMode(p, OUTPUT);
  analogWrite(p, 0);
}

void PwmDevices::off() {
  for (uint8_t i = 0; i < last; i++) {
    // Turn off PWM output.
    analogWrite(port[i], 0);
    activated[i] = 0;
    currentPower[i] = 0;
    scheduled[i] = 0;
    fastSwitchManagedActive[i] = false;
  }
}

void PwmDevices::reset() {
  off();

  for (uint8_t i = 0; i < MAX_PWM_OUTPUTS; i++) {
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
    currentPower[i] = 0;
    scheduled[i] = 0;
    fastSwitchClosed[i] = false;
    fastSwitchManagedActive[i] = false;
    fastSwitchWaitForRelease[i] = false;
  }

  last = 0;
}

void PwmDevices::deactivateOutput(byte i) {
  analogWrite(port[i], 0);
  activated[i] = 0;
  currentPower[i] = 0;
  scheduled[i] = false;
  fastSwitchManagedActive[i] = false;
}

void PwmDevices::update() {
  _ms = millis();

  // Iterate over all outputs.
  for (byte i = 0; i < last; i++) {
    if (activated[i] > 0) {
      // The output is active.
      uint32_t timePassed = _ms - activated[i];
      const bool minPulseElapsed =
          (minPulseTime[i] == 0) || (timePassed > minPulseTime[i]);
      if ((maxPulseTime[i] > 0) && (timePassed > maxPulseTime[i])) {
        // Enforce the maximum pulse time even if the fast-flip switch remains
        // closed. A new physical release/close cycle is required to fire
        // again.
        if (fastSwitchManagedActive[i] && fastSwitchClosed[i]) {
          fastSwitchWaitForRelease[i] = true;
        }
        deactivateOutput(i);
        CrossLinkDebugger::debug(
            "Performed max pulse deactivation of PWM device on port %d after "
            "%dms",
            port[i], timePassed);
      } else if (scheduled[i] && minPulseElapsed) {
        // Deactivate the output if it is scheduled for delayed deactivation and
        // the minimum pulse time is reached.
        deactivateOutput(i);
        CrossLinkDebugger::debug(
            "Performed scheduled deactivation of PWM device on port %d after "
            "%dms",
            port[i], timePassed);
      } else if (fastSwitchManagedActive[i] && minPulseElapsed &&
                 !fastSwitchClosed[i]) {
        // Ignore fast-switch toggles during the minimum pulse time, then honor
        // the latest open state once the minimum pulse has elapsed.
        deactivateOutput(i);
        CrossLinkDebugger::debug(
            "Performed min pulse guarded deactivation of PWM device on port %d "
            "after %dms",
            port[i], timePassed);
      } else if ((holdPowerActivationTime[i] > 0) &&
                 (currentPower[i] > holdPower[i]) &&
                 (timePassed > holdPowerActivationTime[i])) {
        // Reduce the power of the activated output if the hold power activation
        // time pased since the activation.
        analogWrite(port[i], holdPower[i]);
        currentPower[i] = holdPower[i];
        CrossLinkDebugger::debug(
            "Reduced power of PWM device on port %d to power %d after %dms",
            port[i], holdPower[i], timePassed);
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
    currentPower[i] = power[i];
    scheduled[i] = false;
    fastSwitchManagedActive[i] = false;
    CrossLinkDebugger::debug("Activated PWM device on port %d with power %d",
                             port[i], power[i]);
  } else if (!targetState && activated[i] > 0) {
    // Event received to deactivate the output.
    // Check if a minimum pulse time is configured for this output.
    if ((_ms >= activated[i]) && (minPulseTime[i] > 0) &&
        (_ms - activated[i]) < minPulseTime[i]) {
      // A minimum pulse time is configured for this output.
      // Don't deactivate it immediately but schedule its later deactivation.
      scheduled[i] = true;
      CrossLinkDebugger::debug("Scheduled PWM device state change port %d",
                             port[i]);
    } else {
      // Deactivate the output.
      deactivateOutput(i);
      CrossLinkDebugger::debug("Deactivated PWM device on port %d", port[i]);
    }
  }
}

void PwmDevices::handleFastSwitchEvent(bool switchClosed, byte i) {
  fastSwitchClosed[i] = switchClosed;

  if (!switchClosed) {
    // Re-arm after the stuck/held switch has been released.
    fastSwitchWaitForRelease[i] = false;

    if (activated[i] == 0 || !fastSwitchManagedActive[i]) {
      return;
    }

    const uint32_t timePassed = _ms - activated[i];
    if ((minPulseTime[i] > 0) && (timePassed <= minPulseTime[i])) {
      // Ignore toggles during the minimum pulse time. The update loop will
      // turn the output off once the minimum pulse has elapsed.
      return;
    }

    deactivateOutput(i);
    CrossLinkDebugger::debug(
        "Deactivated fast-switch PWM device on port %d after switch release",
        port[i]);
    return;
  }

  if (fastSwitchWaitForRelease[i]) {
    CrossLinkDebugger::debug(
        "Ignored fast-switch activation on port %d until switch release",
        port[i]);
    return;
  }

  if (activated[i] == 0) {
    analogWrite(port[i], power[i]);
    activated[i] = _ms;
    currentPower[i] = power[i];
    scheduled[i] = false;
    fastSwitchManagedActive[i] = true;
    CrossLinkDebugger::debug(
        "Activated fast-switch PWM device on port %d with power %d", port[i],
        power[i]);
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
            handleFastSwitchEvent((bool)event->value, i);
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
      analogWrite(port[i], 0);
      // Mark the output as deactivated.
      deactivateOutput(i);
      fastSwitchWaitForRelease[i] = false;
      fastSwitchClosed[i] = false;
      powerOn = false;
      CrossLinkDebugger::debug("Deactivated PWM device on port %d", port[i]);
    }
  }
}
