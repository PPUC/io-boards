// Markus Kalkbrenner 2023

#include <PPUC.h>

#include "IOBoardController.h"
#include "EffectsController.h"

IOBoardController ioBoardController(CONTROLLER_16_8_1);
// Platform will be adjusted by ConfigEvent.
EffectsController effectsController(CONTROLLER_16_8_1, PLATFORM_LIBPINMAME);

// Each controller will be bound to its own core and has it's own
// EventDispatcher. Only the EventDispatcher of IOBoardController
// is attached to RS485. But both EventDispatchers must share the
// same MultiCoreCrosslink to send and receive events between
// both cores.

void setup() {
    //ioBoardController.eventDispatcher()->addListener(new CrossLinkDebugger());
}

void setup1() {
    effectsController.eventDispatcher()->setMultiCoreCrossLink(
        ioBoardController.eventDispatcher()->getMultiCoreCrossLink()
    );
}

void loop() {
    ioBoardController.update();
    //Serial.println(16 - ((int) (analogRead(28) + 20) / 60));
}

void loop1() {
    effectsController.update();
}
