// Markus Kalkbrenner 2023

#include <PPUC.h>

#include "IOBoardController.h"
#include "EffectsController.h"
#include "EventDispatcher/CrossLinkDebugger.h"

IOBoardController ioBoardController(CONTROLLER_16_8_1);
// Platform will be adjusted by ConfigEvent.
EffectsController effectsController(CONTROLLER_16_8_1, PLATFORM_LIBPINMAME);

// Each controller will be bound to its own core and has it's own
// EventDispatcher. Only the EventDispatcher of IOBoardController
// is attached to RS485. But both EventDispatchers must share the
// same MultiCoreCrosslink to send and receive events between
// both cores.

void setup()
{
    Serial.begin(115200);
    // The Pico implements USB itself so special care must be taken. Use while(!Serial){} in the setup() code before printing anything so that it waits for the USB connection to be established.
    // https://community.platformio.org/t/serial-monitor-not-working/1512/25
    while (!Serial)
    {
    }

    Serial1.setTX(0);
    Serial1.setRX(1);
    Serial1.setFIFOSize(128); // @todo find the right size.
    Serial1.begin(115200);
    // The Pico implements USB itself so special care must be taken. Use while(!Serial){} in the setup() code before printing anything so that it waits for the USB connection to be established.
    // https://community.platformio.org/t/serial-monitor-not-working/1512/25
    while (!Serial1)
    {
    }

    ioBoardController.eventDispatcher()->addListener(new CrossLinkDebugger());
}

void setup1()
{
    effectsController.eventDispatcher()->setMultiCoreCrossLink(
        ioBoardController.eventDispatcher()->getMultiCoreCrossLink());
}

void loop()
{
    ioBoardController.update();
}

void loop1()
{
    effectsController.update();
}
