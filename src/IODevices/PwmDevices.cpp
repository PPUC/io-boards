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
                                  byte fS, byte sS1, byte sS2, byte outputType,
                                  bool ignoreGO, bool boardFl) {
  int index = findRegisteredOutput(outputType, p, n);
  if (index < 0) {
    if (last >= MAX_PWM_OUTPUTS) return;
    index = last++;
  }

  if (outputType == PWM_TYPE_SOLENOID && (sS1 > 0 || sS2 > 0) && maxPT == 0) {
    // A coil a switch has to stop must never be left unbounded. A motor is not
    // covered: whatever drives it decides how long a run is.
    maxPT = STOP_SWITCHED_MAX_PULSE_MS;
    CrossLinkDebugger::debug(
        "PWM device on port %d has a stop switch but no max pulse time, using "
        "%dms",
        p, maxPT);
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
  ignoreGameOn[index] = ignoreGO;
  romOn[index] = false;
  // Only a coil its switch drives can be the board's alone.
  boardFlipper[index] = boardFl && fS > 0 && outputType == PWM_TYPE_SOLENOID;
  powerScale[index] = 255;
  activated[index] = 0;
  currentPower[index] = 0;
  scheduled[index] = false;
  fastSwitchClosed[index] = false;
  fastSwitchManagedActive[index] = false;
  fastSwitchWaitForRelease[index] = false;
  fastSwitchClosedAt[index] = 0;
  fastSwitchPulseEndedAt[index] = 0;
  for (byte s = 0; s < MAX_STOP_SWITCHES_PER_OUTPUT; s++) {
    stopSwitchClosed[index][s] = false;
  }
  stopEngaged[index] = false;
  stopRedrives[index] = 0;

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
  ignoreGameOn[index] = false;
  romOn[index] = false;
  boardFlipper[index] = false;
  powerScale[index] = 255;
  fastSwitch[index] = 0;
  type[index] = PWM_TYPE_FLASHER;
  activated[index] = 0;
  currentPower[index] = 0;
  scheduled[index] = false;
  fastSwitchClosed[index] = false;
  fastSwitchManagedActive[index] = false;
  fastSwitchWaitForRelease[index] = false;
  fastSwitchClosedAt[index] = 0;
  fastSwitchPulseEndedAt[index] = 0;

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
  ignoreGameOn[index] = false;
  romOn[index] = false;
  boardFlipper[index] = false;
  powerScale[index] = 255;
  fastSwitch[index] = 0;
  type[index] = PWM_TYPE_LAMP;
  activated[index] = 0;
  currentPower[index] = 0;
  scheduled[index] = false;
  fastSwitchClosed[index] = false;
  fastSwitchManagedActive[index] = false;
  fastSwitchWaitForRelease[index] = false;
  fastSwitchClosedAt[index] = 0;
  fastSwitchPulseEndedAt[index] = 0;

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
    ignoreGameOn[i] = false;
    boardFlipper[i] = false;
    romOn[i] = false;
    powerScale[i] = 255;
    fastSwitch[i] = 0;
    type[i] = 0;
    activated[i] = 0;
    currentPower[i] = 0;
    scheduled[i] = 0;
    fastSwitchClosed[i] = false;
    fastSwitchManagedActive[i] = false;
    fastSwitchWaitForRelease[i] = false;
    fastSwitchClosedAt[i] = 0;
    fastSwitchPulseEndedAt[i] = 0;
    stopRedrives[i] = 0;
  }

  last = 0;
  flippersEnabled = false;
}

void PwmDevices::deactivateOutput(byte i) {
  if (activated[i] > 0 && fastSwitchManagedActive[i]) {
    // 0 means "never", and millis() can be 0 right after boot.
    fastSwitchPulseEndedAt[i] = _ms != 0 ? _ms : 1;
  }
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
    CrossLinkDebugger::debug("Stopped PWM device on port %d: switch %d closed",
                             port[i], switchNumber);
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

    const bool fastSwitchAllowed = fastSwitchAllowedFor(i);

    if (_eventDispatcher && fastSwitchAllowed && fastSwitch[i] > 0 &&
        (type[i] == PWM_TYPE_SOLENOID || type[i] == PWM_TYPE_MOTOR)) {
      // Fast-flip coils should not depend forever on one switch edge event.
      // Reconcile against the current board/global switch bitmap each update so
      // a missed release event cannot leave a hold-style flipper powered until
      // the next manual switch cycle.
      noteFastSwitchState(_eventDispatcher->getSwitchState(
                              static_cast<uint16_t>(fastSwitch[i])),
                          i);
      if (!fastSwitchClosed[i]) {
        fastSwitchWaitForRelease[i] = false;
        stopRedrives[i] = 0;
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
      if (stopRedrives[i] >= MAX_STOP_REDRIVES_PER_PRESS) {
        // A contact that keeps opening is chattering or misadjusted, not a
        // ball. Wait for the button, as after a timeout.
        fastSwitchWaitForRelease[i] = true;
        CrossLinkDebugger::debug(
            "PWM device on port %d fired again %d times in one press, waiting "
            "for release",
            port[i], stopRedrives[i]);
        continue;
      }
      if (inRefireLockout(i)) {
        continue;
      }
      stopRedrives[i]++;
      currentPower[i] = scaledPower(i);
      analogWrite(port[i], currentPower[i]);
      activated[i] = _ms;
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
      } else if (type[i] == PWM_TYPE_MOTOR && holdPowerActivationTime[i] == 0 &&
                 currentPower[i] != scaledPower(i)) {
        analogWrite(port[i], scaledPower(i));
        currentPower[i] = scaledPower(i);
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

byte PwmDevices::scaledPower(byte i) const {
  if (powerScale[i] == 255 || holdPowerActivationTime[i] > 0) {
    return power[i];
  }
  // The host is still measuring the speed this motor should run at.
  if (powerScale[i] == 0) {
    return 0;
  }
  // Never to 0 by rounding: the coil bit says whether a motor runs, not this.
  const byte scaled = (uint16_t)power[i] * powerScale[i] / 255;
  return scaled > 0 ? scaled : 1;
}

void PwmDevices::handlePowerScaleEvent(uint16_t motorNumber, byte scale) {
  for (byte i = 0; i < last; i++) {
    if (type[i] != PWM_TYPE_MOTOR || number[i] != (byte)motorNumber) {
      continue;
    }
    powerScale[i] = scale;
    // update() applies it to a motor that is running.
  }
}

bool PwmDevices::outputGateOpen(byte i) const {
  if (boardFlipper[i]) {
    return flippersEnabled && running && coinDoorClosed;
  }
  return highPowerAvailable();
}

bool PwmDevices::fastSwitchAllowedFor(byte i) const {
  return outputGateOpen(i) && !tiltActive;
}

// The host's word that a game is running, or no longer is.
//
// Board flippers follow their buttons from here on, and nothing the ROM does -
// its own flipper pulses, the game-on solenoid dropping between balls or with
// a resync - reaches them. Turning them off drops them at once, and either way
// a button already held has to be pressed again, as after a tilt.
void PwmDevices::handleFlipperEnableEvent(bool enabled) {
  if (enabled == flippersEnabled) {
    return;
  }
  flippersEnabled = enabled;
  _ms = millis();
  for (byte i = 0; i < last; i++) {
    if (!boardFlipper[i]) {
      continue;
    }
    if (!enabled) {
      deactivateOutput(i);
    }
    fastSwitchClosed[i] = false;
    fastSwitchWaitForRelease[i] = true;
  }
  CrossLinkDebugger::debug("Board flippers %s", enabled ? "on" : "off");
}

// Remembers what the ROM asks of an output even while a gate discards it.
//
// The host sends full snapshots, but the board acts only on what changed in
// them. A request that a closed gate threw away is therefore never sent again:
// once the gate opened, an output the ROM was holding on stayed off until the
// ROM happened to switch it off and on again.
void PwmDevices::noteRomRequest(const Event *event) {
  if (event->sourceId != EVENT_SOURCE_SOLENOID &&
      event->sourceId != EVENT_SOURCE_LIGHT) {
    return;
  }
  const bool lamp = event->sourceId == EVENT_SOURCE_LIGHT;
  for (byte i = 0; i < last; i++) {
    if ((type[i] == PWM_TYPE_LAMP) != lamp ||
        number[i] != (byte)event->eventId) {
      continue;
    }
    romOn[i] = event->value != 0;
  }
}

// The coin door has just closed: high power is back, and every output the ROM
// still wants on goes on again.
//
// Opening the door cuts everything, and closing it brought nothing back: a trap
// door hold, a magnet, a motor, the start button lamp stayed off for as long as
// the ROM kept them on. Only what the ROM wants now is driven, through the same
// path as a fresh request, so the pulse limits and the ROM-copy filter apply;
// a pulse that ended while the door was open is not repeated. Board flippers
// do not take the ROM's orders and are left alone.
void PwmDevices::restoreRomOutputs() {
  const bool exemptAvailable = gameOnExemptAvailable();
  for (byte i = 0; i < last; i++) {
    if (!romOn[i] || boardFlipper[i]) {
      continue;
    }
    if (type[i] == PWM_TYPE_LAMP) {
      if (highPowerAvailable()) {
        analogWrite(port[i], power[i]);
      }
      continue;
    }
    if (type[i] != PWM_TYPE_SOLENOID && type[i] != PWM_TYPE_FLASHER &&
        type[i] != PWM_TYPE_MOTOR) {
      continue;
    }
    if (highPowerAvailable() || (ignoreGameOn[i] && exemptAvailable)) {
      updateSolenoidOrFlasher(true, i);
      CrossLinkDebugger::debug(
          "Coin door closed: restored PWM device on port %d", port[i]);
    }
  }
}

void PwmDevices::updateSolenoidOrFlasher(bool targetState, byte i) {
  _ms = millis();

  if (isRomCopyOfFastSwitchPulse(targetState, i)) {
    CrossLinkDebugger::debug(
        "Ignored ROM %s of fast-switch PWM device on port %d: the board drives "
        "it from its switch",
        targetState ? "activation" : "deactivation", port[i]);
    return;
  }

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
    currentPower[i] = scaledPower(i);
    analogWrite(port[i], currentPower[i]);
    // Rememebr when it got activated.
    activated[i] = _ms;
    scheduled[i] = false;
    fastSwitchManagedActive[i] = false;
    CrossLinkDebugger::debug("Activated PWM device on port %d with power %d",
                             port[i], currentPower[i]);
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

void PwmDevices::noteFastSwitchState(bool switchClosed, byte i) {
  if (switchClosed && !fastSwitchClosed[i]) {
    // 0 means "never", and millis() can be 0 right after boot.
    fastSwitchClosedAt[i] = _ms != 0 ? _ms : 1;
  }
  fastSwitchClosed[i] = switchClosed;
}

// Whether a ROM request for a fast-flip output is the ROM's own copy of what
// the board already did for the switch, rather than a request of its own.
//
// "off": while the board drives the output from its switch, that pulse is the
// switch's, and ends when the switch opens, the stop switch closes or the
// maximum pulse time runs out. A ROM "off" there is the end of the ROM's copy,
// and it dropped a flipper the player was still holding. Nothing about this is
// timed.
//
// "on": while the switch is closed, the board is already doing it, unless it
// has given up waiting for a release -- a ball resting on a sling switch -- in
// which case the ROM's ball search is exactly what should get through. Shortly
// after the switch closed, it is the ROM's copy arriving late, and obeying it
// fired a sling or a jet a second time.
//
// Everything else is the ROM's own: the coil test, a ball search, a ROM that
// fires the coil with no switch in front of it.
bool PwmDevices::isRomCopyOfFastSwitchPulse(bool targetState, byte i) const {
  if (fastSwitch[i] == 0) {
    return false;
  }
  if (!targetState) {
    return activated[i] > 0 && fastSwitchManagedActive[i];
  }
  if (fastSwitchClosed[i] && !fastSwitchWaitForRelease[i]) {
    return true;
  }
  return fastSwitchClosedAt[i] != 0 &&
         (_ms - fastSwitchClosedAt[i]) < FAST_SWITCH_ROM_COPY_WINDOW_MS;
}

// Whether output i's switch holds any output on this board - its own, or the
// other winding of the same flipper. A power winding without an end-of-stroke
// switch is still a flipper's when its hold winding is on the same button.
bool PwmDevices::fastSwitchHoldsAnOutput(byte i) const {
  for (byte j = 0; j < last; j++) {
    if (fastSwitch[j] != fastSwitch[i] ||
        (type[j] != PWM_TYPE_SOLENOID && type[j] != PWM_TYPE_MOTOR)) {
      continue;
    }
    if (FastSwitchOutputHolds(maxPulseTime[j], holdPowerActivationTime[j],
                              stopSwitch[j][0], stopSwitch[j][1])) {
      return true;
    }
  }
  return false;
}

// A sling or a jet whose kick ended moments ago, see
// FAST_SWITCH_REFIRE_LOCKOUT_MS.
bool PwmDevices::inRefireLockout(byte i) const {
  return fastSwitchPulseEndedAt[i] != 0 &&
         (_ms - fastSwitchPulseEndedAt[i]) < FAST_SWITCH_REFIRE_LOCKOUT_MS &&
         !fastSwitchHoldsAnOutput(i);
}

void PwmDevices::handleFastSwitchEvent(bool switchClosed, byte i) {
  noteFastSwitchState(switchClosed, i);

  if (!switchClosed) {
    // Re-arm after the stuck/held switch has been released.
    fastSwitchWaitForRelease[i] = false;
    stopRedrives[i] = 0;

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

  if (inRefireLockout(i)) {
    // And wait for a release: the leaf still reading closed once the lockout
    // is over is the same chatter, and must not be re-driven into a kick.
    fastSwitchWaitForRelease[i] = true;
    CrossLinkDebugger::debug(
        "Ignored fast-switch activation on port %d: its last kick just ended",
        port[i]);
    return;
  }

  if (activated[i] == 0) {
    currentPower[i] = scaledPower(i);
    analogWrite(port[i], currentPower[i]);
    activated[i] = _ms;
    scheduled[i] = false;
    fastSwitchManagedActive[i] = true;
    CrossLinkDebugger::debug(
        "Activated fast-switch PWM device on port %d with power %d", port[i],
        currentPower[i]);
  }
}

void PwmDevices::handleEvent(Event *event) {
  if (event->sourceId == EVENT_SOURCE_POWER_SCALE) {
    handlePowerScaleEvent(event->eventId, event->value);
    return;
  }
  if (event->sourceId == EVENT_SOURCE_FLIPPER_ENABLE) {
    handleFlipperEnableEvent(event->value != 0);
    return;
  }

  const bool doorWasClosed = coinDoorClosed;
  HighPowerOffAware::handleEvent(event);
  publishGateBits();

  _ms = millis();

  noteRomRequest(event);
  if (!doorWasClosed && coinDoorClosed) {
    restoreRomOutputs();
  }

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

  if (event->sourceId == EVENT_SOURCE_SWITCH) {
    // A switch event was triggered or received. Activate or deactivate any
    // output configured as "fastSwitch" for that switch, and cut any output
    // this switch is a stop for. Each output behind its own gate: a board
    // flipper's is the flipper enable, everything else's high power.
    for (byte i = 0; i < last; i++) {
      if (type[i] != PWM_TYPE_SOLENOID && type[i] != PWM_TYPE_MOTOR) {
        continue;
      }
      if (!outputGateOpen(i)) {
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
  }

  if (powerOn && coinDoorClosed) {
    switch (event->sourceId) {
      case EVENT_SOURCE_SOLENOID:
        for (byte i = 0; i < last; i++) {
          // A board flipper does not take orders from the ROM, not even the
          // copy of its own pulse.
          if (boardFlipper[i]) {
            continue;
          }
          if ((type[i] == PWM_TYPE_SOLENOID || type[i] == PWM_TYPE_FLASHER ||
               type[i] == PWM_TYPE_MOTOR) &&
              number[i] == (byte)event->eventId) {
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
  } else {
    const bool exemptAvailable = gameOnExemptAvailable();

    if (exemptAvailable && event->sourceId == EVENT_SOURCE_SOLENOID) {
      for (byte i = 0; i < last; i++) {
        if (ignoreGameOn[i] && !boardFlipper[i] &&
            number[i] == (byte)event->eventId) {
          updateSolenoidOrFlasher((bool)event->value, i);
        }
      }
    }

    if (powerToggled) {
      // powerOn is the game-on solenoid's state and is left alone here. This
      // used to clear it, so opening the coin door also forgot that game-on
      // was set. The host only sends changes, and game-on does not change
      // when the door closes, so every coil on the board stayed dead until
      // the ROM next switched game-on - in a game, the next ball.
      for (byte i = 0; i < last; i++) {
        // A game ending must not stop what the ROM is still running.
        if (ignoreGameOn[i] && exemptAvailable) {
          continue;
        }
        // Nor does it end the board's flippers: a drained ball, a resync.
        if (boardFlipper[i] && outputGateOpen(i)) {
          continue;
        }
        // Deactivate the output.
        analogWrite(port[i], 0);
        // Mark the output as deactivated.
        deactivateOutput(i);
        fastSwitchWaitForRelease[i] = false;
        fastSwitchClosed[i] = false;
        CrossLinkDebugger::debug("Deactivated PWM device on port %d", port[i]);
      }
    }
  }
}
