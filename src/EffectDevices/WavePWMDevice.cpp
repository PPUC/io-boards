#include "WavePWMDevice.h"

#include "../EventDispatcher/CrossLinkDebugger.h"

void WavePWMDevice::on() { reset(); }

void WavePWMDevice::reset() { setPWM(0); }

void WavePWMDevice::setPWM(uint8_t pwm) {
  if (powerOn && coinDoorClosed) {
    analogWrite(pin, pwm);
  }
  currentPWM = pwm;
}

uint8_t WavePWMDevice::getPWM() { return currentPWM; }

WavePWM* WavePWMDevice::getWavePWM() { return wavePWM; }

void WavePWMDevice::handleEvent(Event* event) {
  HighPowerOffAware::handleEvent(event);

  if (!(powerOn && coinDoorClosed) && powerToggled) {
    // Deactivate the output.
    digitalWrite(pin, 0);
    powerOn = false;
    CrossLinkDebugger::debug("Deactivated PWM device on port %d", pin);
  }
}