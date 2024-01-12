// Markus Kalkbrenner 2022

#include <PPUC.h>

#include "EffectsController.h"
#include "IOBoardController.h"

IOBoardController ioBoardController(CONTROLLER_16_8_1);
EffectsController effectsController(CONTROLLER_16_8_1, PLATFORM_LIBPINMAME);

void setup() {
  ioBoardController.eventDispatcher()->addListener(new CrossLinkDebugger());
}

void setup1() {}

void loop() {
  ioBoardController.update();
  // Serial.println(16 - ((int) (analogRead(28) + 30) / 60));
}

void loop1() { effectsController.update(); }
