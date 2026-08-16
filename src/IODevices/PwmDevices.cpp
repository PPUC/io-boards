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
                                  byte fS, byte sS1, byte sS2,
                                  byte outputType) {
  int index = findRegisteredOutput(outputType, p, n);
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
  stopSwitch[index][0] = sS1;
  stopSwitch[index][1] = sS2;
  type[index] = outputType;
  activated[index] = 0;
  currentPower[index] = 0;
  scheduled[index] = false;
  fastSwitchClosed[index] = false;
  fastSwitchManagedActive[index] = false;
  fastSwitchWaitForRelease[index] = false;
  for (byte s = 0; s < MAX_STOP_SWITCHES_PER_OUTPUT; s++) {
    stopSwitchClosed[index][s] = false;
  }
  stopEngaged[index] = false;

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

bool PwmDevices::hasStopSwitch(byte i) const {
  for (byte s = 0; s < MAX_STOP_SWITCHES_PER_OUTPUT; s++) {
    if (stopSwitch[i][s] > 0) return true;
  }
  return false;
}

bool PwmDevices::anyStopSwitchClosed(byte i) const {
  for (byte s = 0; s < MAX_STOP_SWITCHES_PER_OUTPUT; s++) {
    if (stopSwitch[i][s] > 0 && stopSwitchClosed[i][s]) return true;
  }
  return false;
}

// One stop switch changed state.
//
// Engaging is deliberately on the *closing edge*, not on the switch being
// closed. An assembly usually starts its travel sitting on one of its end
// switches, and a level test would refuse to let it move at all.
//
// Releasing is on the level: once every stop switch is open the output may run
// again. For an output driven by a fast-flip switch that means it fires again
// by itself, which is the point on a Fliptronic flipper - a ball heavy enough
// to push the finger back down opens the EOS, and the flipper should come back
// up while the button is still held rather than staying down until the player
// lets go and presses again.
void PwmDevices::handleStopSwitchEvent(byte switchNumber, bool switchClosed,
                                       byte i) {
  bool closingEdge = false;
  for (byte s = 0; s < MAX_STOP_SWITCHES_PER_OUTPUT; s++) {
    if (stopSwitch[i][s] != switchNumber) continue;
    if (switchClosed && !stopSwitchClosed[i][s]) {
      closingEdge = true;
    }
    stopSwitchClosed[i][s] = switchClosed;
  }

  if (closingEdge && activated[i] > 0) {
    stopEngaged[i] = true;
    deactivateOutput(i);
    CrossLinkDebugger::debug(
        "Stopped PWM device on port %d: switch %d closed", port[i],
        switchNumber);
    return;
  }

  if (stopEngaged[i] && !anyStopSwitchClosed(i)) {
    stopEngaged[i] = false;
    CrossLinkDebugger::debug("Released stop on PWM device on port %d", port[i]);
  }
}

// Reconciles the stop switches against what the board currently sees.
//
// The same reason the fast-flip switch is reconciled every update: an edge
// event that never arrives must not leave an output stopped forever, or held on
// when it should have been cut.
void PwmDevices::refreshStopSwitches(byte i) {
  if (!_eventDispatcher || !hasStopSwitch(i)) return;

  for (byte s = 0; s < MAX_STOP_SWITCHES_PER_OUTPUT; s++) {
    if (stopSwitch[i][s] == 0) continue;
    const bool closed = _eventDispatcher->getSwitchState(
        static_cast<uint16_t>(stopSwitch[i][s]));
    if (closed && !stopSwitchClosed[i][s] && activated[i] > 0) {
      stopEngaged[i] = true;
      deactivateOutput(i);
      CrossLinkDebugger::debug(
          "Stopped PWM device on port %d: switch %d found closed", port[i],
          stopSwitch[i][s]);
    }
    stopSwitchClosed[i][s] = closed;
  }

  if (stopEngaged[i] && !anyStopSwitchClosed(i)) {
    stopEngaged[i] = false;
    CrossLinkDebugger::debug("Released stop on PWM device on port %d", port[i]);
  }
}

void PwmDevices::update() {
  _ms = millis();

  // Iterate over all outputs.
  for (byte i = 0; i < last; i++) {
    refreshStopSwitches(i);

    const bool fastSwitchAllowed = fastSwitchOutputsAllowed();

    if (_eventDispatcher && fastSwitchAllowed && fastSwitch[i] > 0 &&
        (type[i] == PWM_TYPE_SOLENOID || type[i] == PWM_TYPE_MOTOR)) {
      // Fast-flip coils should not depend forever on one switch edge event.
      // Reconcile against the current board/global switch bitmap each update so
      // a missed release event cannot leave a hold-style flipper powered until
      // the next manual switch cycle.
      fastSwitchClosed[i] =
          _eventDispatcher->getSwitchState(static_cast<uint16_t>(fastSwitch[i]));
      if (!fastSwitchClosed[i]) {
        fastSwitchWaitForRelease[i] = false;
      }
    } else if (!fastSwitchAllowed && fastSwitch[i] > 0) {
      // High power is off, or the machine is tilted. Forget that the switch was
      // closed and demand a fresh press before firing again: without this, a
      // player still holding the button when power or tilt clears gets an
      // immediate flip, which is exactly what the maxPulseTime path already
      // guards against.
      fastSwitchClosed[i] = false;
      fastSwitchWaitForRelease[i] = true;
    }

    if (fastSwitchAllowed && activated[i] == 0 && !stopEngaged[i] &&
        fastSwitch[i] > 0 && fastSwitchClosed[i] &&
        !fastSwitchWaitForRelease[i] &&
        (type[i] == PWM_TYPE_SOLENOID || type[i] == PWM_TYPE_MOTOR)) {
      // The stop cleared while the driving switch is still closed, so drive it
      // again. Only for a switch-driven output: one the host commands waits for
      // the host to ask again, because a motor that stopped at the end of its
      // travel has arrived, not failed.
      analogWrite(port[i], power[i]);
      activated[i] = _ms;
      currentPower[i] = power[i];
      scheduled[i] = false;
      fastSwitchManagedActive[i] = true;
      CrossLinkDebugger::debug(
          "Re-activated PWM device on port %d after its stop switch opened",
          port[i]);
    }

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

  if (targetState && stopEngaged[i]) {
    // A stop switch is holding this output off. Ignoring the request rather
    // than queueing it: the assembly is sitting at the end of its travel, and
    // driving into that is what the switch is there to prevent.
    CrossLinkDebugger::debug(
        "Ignored activation of PWM device on port %d: stopped by a switch",
        port[i]);
    return;
  }

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

  if (stopEngaged[i]) {
    CrossLinkDebugger::debug(
        "Ignored fast-switch activation on port %d: stopped by a switch",
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

  // Tilt just latched. Drop only the fast-flip outputs -- flippers, slingshots,
  // pop bumpers. Checked here rather than as an "else" on the power branch
  // because tilt deliberately leaves powerOn alone: the outhole kicker and
  // trough eject have to keep working so the machine can get its balls back.
  if (tiltActive && tiltToggled) {
    for (byte i = 0; i < last; i++) {
      if (fastSwitch[i] == 0) {
        continue;
      }
      analogWrite(port[i], 0);
      deactivateOutput(i);
      fastSwitchClosed[i] = false;
      // Demand a fresh press: otherwise a player still holding the button gets
      // an immediate flip the moment tilt clears.
      fastSwitchWaitForRelease[i] = true;
      CrossLinkDebugger::debug(
          "Tilt: deactivated fast-switch PWM device on port %d", port[i]);
    }
  }

  if (powerOn && coinDoorClosed) {
    switch (event->sourceId) {
      case EVENT_SOURCE_SOLENOID:
        for (byte i = 0; i < last; i++) {
          if ((type[i] == PWM_TYPE_SOLENOID || type[i] == PWM_TYPE_FLASHER ||
               type[i] == PWM_TYPE_MOTOR) &&
              number[i] == (byte)event->eventId) {
            updateSolenoidOrFlasher((bool)event->value, i);
          }
        }
        break;

      case EVENT_SOURCE_SWITCH:
        // A switch event was triggered or received. Activate or deactivate any
        // output configured as "fastSwitch" for that switch, and cut any output
        // this switch is a stop for.
        for (byte i = 0; i < last; i++) {
          if (type[i] != PWM_TYPE_SOLENOID && type[i] != PWM_TYPE_MOTOR) {
            continue;
          }
          // Stops first: if one switch both drives and stops an output, the
          // stop is the safety and has to win.
          handleStopSwitchEvent((byte)event->eventId, (bool)event->value, i);
          // Stops still apply while tilted; firing does not.
          if (!tiltActive && fastSwitch[i] == (byte)event->eventId) {
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
