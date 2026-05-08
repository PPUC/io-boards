#include "EffectDevice.h"

void EffectDevice::off() { reset(); }

void EffectDevice::startEffect(bool exclusive) {
  (void)exclusive;
  on();
}

void EffectDevice::stopEffect(bool exclusive) {
  (void)exclusive;
  off();
}
