/*
  FastLedBlinkEffect.h
  Created by Markus Kalkbrenner, 2026.

  Play more pinball!
*/

#ifndef FastLedBlinkEffect_h
#define FastLedBlinkEffect_h

#include <Arduino.h>

#include "Effect.h"

class FastLedBlinkEffect : public Effect {
 public:
  void update();
};

#endif
