// Markus Kalkbrenner 2023

#include <PPUC.h>

#include "EffectsController.h"
#include "EventDispatcher/CrossLinkDebugger.h"
#include "IOBoardController.h"
#include "RPi_Pico_TimerInterrupt.h"

IOBoardController ioBoardController(CONTROLLER_16_8_1);

// Platform will be adjusted by ConfigEvent.
EffectsController effectsController(CONTROLLER_16_8_1, PLATFORM_LIBPINMAME);

RPI_PICO_Timer ITimer(1);

volatile uint32_t watchdog_ms = millis();

// Turn off all High Power Outputs in case the main loop has not finished in 1
// second (or 2 seconds in edge cases).
bool watchdog(struct repeating_timer *t) {
  if ((millis() - watchdog_ms) > 1000) {
    for (int i = 19; i <= 26; i++) digitalWrite(i, LOW);
  }

  return true;
}

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
    ioBoardController.eventDispatcher()->addListener(new CrossLinkDebugger());
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

  // The watchdog interferes with the USB debuging.
  if (!usb_debugging) {
    if (!ITimer.attachInterruptInterval(1000000, watchdog)) {
      // @todo
    }
  }
}

void setup1() {
  while (!core_0_initilized) {
  }

  if (usb_debugging) {
    delay(10);
    effectsController.eventDispatcher()->addListener(new CrossLinkDebugger());
  }

  effectsController.eventDispatcher()->setMultiCoreCrossLink(
      ioBoardController.eventDispatcher()->getMultiCoreCrossLink());

  effectsController.start();
}

void loop() {
  watchdog_ms = millis();
  ioBoardController.update();
}

void loop1() { effectsController.update(); }
