// Markus Kalkbrenner 2022

#include <PPUC.h>

#include "IOBoardController.h"

IOBoardController ioBoardController(CONTROLLER_16_8_1);

void setup() {
}

void loop() {
    ioBoardController.update();
}
