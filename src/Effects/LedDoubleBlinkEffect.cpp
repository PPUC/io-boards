#include "LedDoubleBlinkEffect.h"

void LedDoubleBlinkEffect::update() {
  if (stage == 0) {
    device->on();
    ++stage;
  } else if (stage == 1 && ms >= 80) {
    device->off();
    ++stage;
  } else if (stage == 2 && ms >= 200) {
    device->on();
    ++stage;
  } else if (stage == 3 && ms >= 280) {
    device->off();
    ++stage;
  } else if (ms >= 1200) {
    // Ends rather than looping here: the effect is registered to repeat, and
    // stopping is what hands it back to be started again.
    stop();
  }
}
