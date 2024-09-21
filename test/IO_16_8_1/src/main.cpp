// Markus Kalkbrenner 2023

#include <PPUC.h>

#include "EffectsController.h"
#include "EventDispatcher/CrossLinkDebugger.h"
#include "IOBoardController.h"

IOBoardController ioBoardController(CONTROLLER_16_8_1);
// Platform will be adjusted by ConfigEvent.
EffectsController effectsController(CONTROLLER_16_8_1, PLATFORM_LIBPINMAME);

bool usb_debugging = false;
bool core_0_initilized = false;

// Each controller will be bound to its own core and has it's own
// EventDispatcher. Only the EventDispatcher of IOBoardController
// is attached to RS485. But both EventDispatchers must share the
// same MultiCoreCrosslink to send and receive events between
// both cores.

void setup() {
  uint32_t timeout = millis() + 2000;

  Serial.begin(115200);
  // The Pico implements USB itself so special care must be taken. Use
  // while(!Serial){} in the setup() code before printing anything so that it
  // waits for the USB connection to be established.
  // https://community.platformio.org/t/serial-monitor-not-working/1512/25
  while (!Serial && millis() < timeout) {
  }

  if (Serial) {
    usb_debugging = true;
    ioBoardController.debug();
    delay(10);
    //ioBoardController.eventDispatcher()->addListener(new CrossLinkDebugger());
  }

  core_0_initilized = true;
  rp2040.restartCore1();

  // RS485 connection.
  Serial1.setTX(0);
  Serial1.setRX(1);
  Serial1.setFIFOSize(1024);  // @todo find the right size.
  Serial1.begin(115200);
  // The Pico implements USB itself so special care must be taken. Use
  // while(!Serial){} in the setup() code before printing anything so that it
  // waits for the USB connection to be established.
  // https://community.platformio.org/t/serial-monitor-not-working/1512/25
  while (!Serial1) {
  }
}

void setup1() {
  while (!core_0_initilized) {
  }

  if (usb_debugging) {
    delay(10);
    //effectsController.eventDispatcher()->addListener(new CrossLinkDebugger());
  }

  effectsController.eventDispatcher()->setMultiCoreCrossLink(
      ioBoardController.eventDispatcher()->getMultiCoreCrossLink());

  effectsController.start();
}

void loop() { ioBoardController.update(); }

void loop1() { effectsController.update(); }
