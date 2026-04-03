// Markus Kalkbrenner 2023-2025

// set to officially supported 200MHz clock
// @see SYS_CLK_MHZ https://github.com/raspberrypi/pico-sdk/releases/tag/2.1.1
#define SYS_CLK_MHZ 200

#include "EffectsController.h"
#include "EventDispatcher/CrossLinkDebugger.h"
#include "IOBoardController.h"
#include "PPUC.h"
#include "PPUCProtocolV2.h"
#include "RPi_Pico_TimerInterrupt.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

IOBoardController ioBoardController(CONTROLLER_16_8_1);

// Platform will be adjusted by ConfigEvent.
EffectsController effectsController(CONTROLLER_16_8_1, PLATFORM_LIBPINMAME);

RPI_PICO_Timer ITimer(1);

volatile uint32_t watchdog_ms = millis();
volatile uint32_t lastPoll_ms = millis();
uint32_t led_heartbeat_ms = 0;
bool led_heartbeat_state = false;

// Turn off all High Power Outputs in case the main loop has not finished in 1
// second (or 2 seconds in edge cases).
bool watchdog(struct repeating_timer *t) {
  uint32_t ms = millis();
  if ((ms - watchdog_ms) > 1000 || (ms - lastPoll_ms) > 3000) {
    for (int i = 19; i <= 26; i++) digitalWrite(i, LOW);
  }

  return true;
}

void updateBuiltinLedHeartbeat() {
  const bool fastBlink =
      ioBoardController.eventDispatcher()->sawRs485Activity() ||
      uart_is_readable(uart1);
  const uint32_t intervalMs = fastBlink ? 120 : 800;
  const uint32_t now = millis();
  if ((now - led_heartbeat_ms) < intervalMs) {
    return;
  }

  led_heartbeat_ms = now;
  led_heartbeat_state = !led_heartbeat_state;
  digitalWrite(LED_BUILTIN, led_heartbeat_state ? HIGH : LOW);
}

bool usb_debugging = false;
bool core_0_initialized = false;

// Each controller will be bound to its own core and has it's own
// EventDispatcher. Only the EventDispatcher of IOBoardController
// is attached to RS485. But both EventDispatchers must share the
// same MultiCoreCrosslink to send and receive events between
// both cores.

void setup() {
  // Overclock according to Raspberry Pi Pico SDK recommendations.
  set_sys_clock_khz(SYS_CLK_KHZ, true);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // RS485 connection.
  Serial1.end();  // Deactivete UART to empty TX FIFO after reboot
  delay(5);
  pinMode(RS485_MODE_PIN, OUTPUT);
  digitalWrite(RS485_MODE_PIN, LOW);  // Read mode
  delay(5);
  // Configure UART1 on GPIO 0 and 1 explicitly seems to fix some strange connection issues.
  gpio_set_function(0, GPIO_FUNC_UART);
  gpio_set_function(1, GPIO_FUNC_UART);
  Serial1.setTX(0);
  Serial1.setRX(1);
  Serial1.begin(ppuc::v2::kBaudRate);
  // Empty RX FIFO after reboot
  while (Serial1.available()) {
    Serial1.read();
  }

  usb_debugging = ioBoardController.isDebug();

  if (usb_debugging) {
    Serial.begin(115200);
    delay(100);
    // Wait for a serial connection of a debugger via USB CDC.
    // The Pico implements USB itself so special care must be taken. Use
    // while(!Serial){} in the setup() code before printing anything so that
    // it waits for the USB connection to be established.
    // https://community.platformio.org/t/serial-monitor-not-working/1512/25
    while (!Serial) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(1000);
    }

    Serial.println("USB Serial debugging active.");
   // ioBoardController.eventDispatcher()->addListener(new CrossLinkDebugger());
  } else {
    // The watchdog interferes with the USB debuging, so only start it
    // if USB debugging is not active.
    if (!ITimer.attachInterruptInterval(1000000, watchdog)) {
      // @todo
    }
  }

  core_0_initialized = true;
  rp2040.restartCore1();
}

void setup1() {
  while (!core_0_initialized) {
  }

  if (usb_debugging) {
    //delay(10);
    //effectsController.eventDispatcher()->addListener(new CrossLinkDebugger());
  }

  effectsController.eventDispatcher()->setMultiCoreCrossLink(
      ioBoardController.eventDispatcher()->getMultiCoreCrossLink());

  effectsController.start();
}

void loop() {
  watchdog_ms = millis();
  updateBuiltinLedHeartbeat();
  ioBoardController.update();
  lastPoll_ms = ioBoardController.eventDispatcher()->getLastPoll();
}

void loop1() { effectsController.update(); }
