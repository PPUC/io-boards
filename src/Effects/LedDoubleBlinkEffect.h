/*
  LedDoubleBlinkEffect.h
  Created by Markus Kalkbrenner, 2026.

  Two short pulses and a long pause, for the built-in LED.

  Deliberately unlike the other patterns this LED carries. Solid on means idle
  and clear, the ready pattern is almost entirely on, the run blink steps
  through a second, and an error toggles every 100ms. A pair of pulses with a
  gap after them is readable across a room and cannot be mistaken for any of
  those at a glance.

  Play more pinball!
*/

#ifndef LedDoubleBlinkEffect_h
#define LedDoubleBlinkEffect_h

#include <Arduino.h>

#include "Effect.h"

class LedDoubleBlinkEffect : public Effect {
 public:
  void update();
};

#endif
