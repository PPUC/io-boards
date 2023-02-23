/*
  Created by Markus Kalkbrenner.
*/

#include <PPUC.h>

#define PPUC_CONTROLLER CONTROLLER_NANO_PIN2DMD_OUTPUT
#define PPUC_NUM_LEDS_1 40
#define PPUC_LED_TYPE_1 WS2812_GRB

#include <InputController.h>
#include <EffectsController.h>

EffectsController effectsController(PPUC_CONTROLLER, PLATFORM_DATA_EAST);
InputController inputController(PPUC_CONTROLLER, PLATFORM_DATA_EAST, effectsController.eventDispatcher());

void setup() {
    inputController.pin2Dmd()->setSerial(Serial);

    effectsController.createCombinedGiAndLightMatrixWs2812FXDevice(1);
    //effectsController.setBrightness(1, 128);

    effectsController.attachBrightnessControl(1, 1);

    effectsController.giAndLightMatrix(1)->setHeatUp(40);
    effectsController.giAndLightMatrix(1)->setAfterGlow(280);

    effectsController.giAndLightMatrix(1)->assignLedRangeToGiString(1, 0, 39);

    effectsController.addEffect(
        new WS2812FXEffect(FX_MODE_STATIC, WHITE, 0, 0),
        effectsController.giAndLightMatrix(1),
        new Event(EVENT_SOURCE_EFFECT, 1, 255),
        1, // priority
        0, // repeat
        -1 // mode
    );

    effectsController.addEffect(
        new LedBlinkEffect(),
        effectsController.ledBuiltInDevice(),
        new Event(EVENT_SOURCE_EFFECT, 1, 255),
        1, // priority
        50, // repeat
        -1 // mode
    );

    effectsController.start();
}

void loop() {
    inputController.pin2Dmd()->update();

    // The effectController also calls update() on the eventDispatcher.
    effectsController.update();
}
