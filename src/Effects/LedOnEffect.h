/*
  LedOnEffect.h
  Created by Markus Kalkbrenner, 2023.

  Play more pinball!
*/

#ifndef LedOnEffect_h
#define LedOnEffect_h

#include <Arduino.h>

#include "Effect.h"

class LedOnEffect : public Effect {
public:
    void update();

};

#endif
