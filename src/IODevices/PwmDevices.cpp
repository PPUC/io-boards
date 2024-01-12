#include "PwmDevices.h"

void PwmDevices::registerSolenoid(byte p, byte n, byte pow, byte minPT,
                                  byte maxPT, byte hP, byte hPAT, byte fS) {
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
    analogWrite(p, 0);
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

void PwmDevices::update() {
  _ms = millis();

  // Iterate over all outputs.
  for (byte i = 0; i < last; i++) {
    if (activated[i] > 0) {
      // The output is active.
      if ((scheduled[i] && ((_ms - activated[i]) > minPulseTime[i])) ||
          ((maxPulseTime[i] > 0) && ((_ms - activated[i]) > maxPulseTime[i]))) {
        // Deactivate the output if it is scheduled for delayed deactivation and
        // the minimum pulse time is reached. Deactivate the output if the
        // maximum pulse time is reached.
        analogWrite(port[i], 0);
        activated[i] = 0;
        scheduled[i] = false;
      } else if ((holdPowerActivationTime[i] > 0) &&
                 ((_ms - activated[i]) > holdPowerActivationTime[i])) {
        // Reduce the power of the activated output if the hold power activation
        // time pased since the activation.
        analogWrite(port[i], holdPower[i]);
      }
    }
  }
}

void PwmDevices::updateSolenoidOrFlasher(bool targetState, byte i) {
  _ms = millis();

  if (targetState && !activated[i]) {
    // Event received to activate the output and output isn't activated already.
    // Activate it!
    analogWrite(port[i], power[i]);
    // Rememebr whin it got activated.
    activated[i] = _ms;
  } else if (!targetState && activated[i]) {
    // Event received to deactivate the output.
    // Check if a minimum pulse time is configured for this output.
    if (minPulseTime[i] > 0 && (_ms - activated[i]) < minPulseTime[i]) {
      // A minimum pulse time is configured for this output.
      // Don't deactivate it immediately but schedule its later deactivation.
      scheduled[i] = true;
    } else {
      // Deactivate the output.
      analogWrite(port[i], 0);
      // Mark the output as deactivated.
      activated[i] = 0;
    }
  }
}

void PwmDevices::handleEvent(Event *event) {
  _ms = millis();

  switch (event->eventId) {
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
}