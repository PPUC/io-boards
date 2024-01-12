#include "LedOnEffect.h"

void LedOnEffect::update() {
  device->on();
  stop();
}
