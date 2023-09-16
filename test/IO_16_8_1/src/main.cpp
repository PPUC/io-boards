// Markus Kalkbrenner 2023

#include <PPUC.h>

#include "IOBoardController.h"
#include "EffectsController.h"
#ifdef USB_DEBUG
#include "EventDispatcher/CrossLinkDebugger.h"
#endif

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
#ifdef USB_DEBUG
    Serial.begin(115200);
    // The Pico implements USB itself so special care must be taken. Use while(!Serial){} in the setup() code before printing anything so that it waits for the USB connection to be established.
    // https://community.platformio.org/t/serial-monitor-not-working/1512/25
    while (!Serial)
    {
    }
    ioBoardController.eventDispatcher()->addListener(new CrossLinkDebugger());
#endif

    Serial1.setTX(0);
    Serial1.setRX(1);
    Serial1.setFIFOSize(128); // @todo find the right size.
    Serial1.begin(115200);
    // The Pico implements USB itself so special care must be taken. Use while(!Serial){} in the setup() code before printing anything so that it waits for the USB connection to be established.
    // https://community.platformio.org/t/serial-monitor-not-working/1512/25
    while (!Serial1)
    {
    }
}

void setup1()
{
#ifdef USB_DEBUG
    while (!Serial)
    {
    }

    effectsController.eventDispatcher()->addListener(new CrossLinkDebugger());
#endif

    effectsController.eventDispatcher()->setMultiCoreCrossLink(
        ioBoardController.eventDispatcher()->getMultiCoreCrossLink());

    effectsController.ledBuiltInDevice()->off();

    effectsController.addEffect(
        new LedOnEffect(),
        effectsController.ledBuiltInDevice(),
        new Event(EVENT_SOURCE_LIGHT, 21, 1),
        1, // priority
        0, // repeat, -1 means endless
        0  // mode
    );

    effectsController.addEffect(
        new NullEffect(),
        effectsController.ledBuiltInDevice(),
        new Event(EVENT_SOURCE_LIGHT, 21, 0),
        1, // priority
        0, // repeat, -1 means endless
        0  // mode
    );

    // effectsController.addEffect(
    //     new LedBlinkEffect(),
    //     effectsController.ledBuiltInDevice(),
    //     new Event(EVENT_ERROR, 1, /* board ID */ 0),
    //     2,  // priority
    //     -1, // repeat, -1 means endless
    //     0   // mode
    // );

    // // Controller start
    // effectsController.addEffect(
    //     new NullEffect(),
    //     effectsController.ledBuiltInDevice(),
    //     new Event(EVENT_NO_ERROR, 1, /* board ID */ 0),
    //     3, // priority
    //     0, // repeat
    //     0  // mode
    // );

    effectsController.start();
}

void loop()
{
    ioBoardController.update();
}

void loop1()
{
    effectsController.update();
}
