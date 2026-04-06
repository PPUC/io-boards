#include "FastLedBlinkEffect.h"

void FastLedBlinkEffect::update() {
  if (stage == 0) {
    device->off();
    stage = 1;
    resetMillis();
    return;
  }

  if (ms < 100) {
    return;
  }

  if ((stage & 1) != 0) {
    device->on();
  } else {
    device->off();
  }

  ++stage;
  resetMillis();
}
