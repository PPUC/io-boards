// Markus Kalkbrenner 2023-2025

// set to officially supported 200MHz clock
// @see SYS_CLK_MHZ https://github.com/raspberrypi/pico-sdk/releases/tag/2.1.1
#define SYS_CLK_MHZ 200

#include "EffectsController.h"
#include "EventDispatcher/CrossLinkDebugger.h"
#include "IOBoardController.h"
#include "hardware/watchdog.h"
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
volatile bool usb_debugging = false;
volatile bool core_0_initialized = false;
volatile bool core_0_loop_running = false;
volatile bool core_1_initialized = false;
volatile bool core_1_loop_running = false;
volatile bool boot_led_active = true;
volatile bool boot_runtime_stage_entered = false;
volatile uint8_t boot_stage = 1;
uint32_t boot_led_ms = 0;
uint32_t boot_runtime_stage_ms = 0;
uint8_t boot_led_pulse_index = 0;
bool boot_led_on = false;
uint32_t ready_led_ms = 0;
bool ready_led_state = true;

enum BootStage : uint8_t {
  BOOT_STAGE_POWER_ON = 1,
  BOOT_STAGE_CORE0_BEGIN = 2,
  BOOT_STAGE_UART_READY = 3,
  BOOT_STAGE_USB_WAIT = 4,
  BOOT_STAGE_CORE1_RESTART = 5,
  BOOT_STAGE_CORE1_BEGIN = 6,
  BOOT_STAGE_CROSSLINK_READY = 7,
  BOOT_STAGE_EFFECTS_STARTED = 8,
  BOOT_STAGE_RUNTIME = 9
};

constexpr uint32_t BOOT_STAGE_RUNTIME_HOLD_MS = 4000;

// How long the main loop must be stopped before the board reboots itself.
//
// Deliberately keyed only on watchdog_ms, never on lastPoll_ms. A quiet bus is
// normal - the host is simply not running - and rebooting for that would put
// every board in a reboot loop whenever ppuc-pinmame is closed. A main loop
// that has not run at all is a hang, and nothing short of a reset clears it:
// the board stops answering the bus entirely and needs its power cycling by
// hand, which is what this avoids.
//
// Well above the 1 s output-shutdown threshold so the outputs are already off
// before a reset is considered.
constexpr uint32_t WATCHDOG_REBOOT_MS = 5000;

// Turn off all High Power Outputs in case the main loop has not finished in 1
// second (or 2 seconds in edge cases), and reboot if it has stopped entirely.
bool watchdog(struct repeating_timer *t) {
  uint32_t ms = millis();
  const uint32_t sinceLoop = ms - watchdog_ms;
  if (sinceLoop > 1000 || (ms - lastPoll_ms) > 3000) {
    for (int i = 19; i <= 26; i++) digitalWrite(i, LOW);
  }

  if (sinceLoop > WATCHDOG_REBOOT_MS) {
    // Outputs are already off by the branch above. The board comes back
    // unconfigured, which the host sees as kStatusNeedsSetup and must treat as
    // "this board lost its configuration", not as a transient.
    watchdog_reboot(0, 0, 0);
  }

  return true;
}

void setBootStage(BootStage stage) {
  boot_stage = static_cast<uint8_t>(stage);
  boot_led_pulse_index = 0;
  boot_led_on = false;
  boot_led_ms = millis();
  digitalWrite(LED_BUILTIN, LOW);
}

void updateBuiltinLedBootPattern() {
  if (!boot_led_active) {
    return;
  }

  const uint32_t now = millis();
  const uint8_t pulses = boot_stage == 0 ? 1 : boot_stage;
  const uint8_t stepsPerCycle = static_cast<uint8_t>(pulses * 2);
  const uint32_t intervalMs =
      boot_led_on ? 90 : (boot_led_pulse_index >= stepsPerCycle ? 850 : 160);
  if ((now - boot_led_ms) < intervalMs) {
    return;
  }

  boot_led_ms = now;
  if (boot_led_pulse_index >= stepsPerCycle) {
    boot_led_pulse_index = 0;
    boot_led_on = false;
    digitalWrite(LED_BUILTIN, LOW);
    return;
  }

  boot_led_on = !boot_led_on;
  digitalWrite(LED_BUILTIN, boot_led_on ? HIGH : LOW);
  boot_led_pulse_index++;
}

void updateBuiltinLedReadyPattern() {
  const uint32_t now = millis();
  const uint32_t intervalMs = ready_led_state ? 1000 : 100;
  if ((now - ready_led_ms) < intervalMs) {
    return;
  }

  ready_led_ms = now;
  ready_led_state = !ready_led_state;
  digitalWrite(LED_BUILTIN, ready_led_state ? HIGH : LOW);
}

void startBuiltinLedReadyPattern() {
  ready_led_state = true;
  ready_led_ms = millis();
  digitalWrite(LED_BUILTIN, HIGH);
}

void delayWithBootPattern(uint32_t delayMs) {
  const uint32_t start = millis();
  while ((millis() - start) < delayMs) {
    updateBuiltinLedBootPattern();
    delay(1);
  }
}

// Each controller will be bound to its own core and has it's own
// EventDispatcher. Only the EventDispatcher of IOBoardController
// is attached to RS485. But both EventDispatchers must share the
// same MultiCoreCrosslink to send and receive events between
// both cores.

void setup() {
  // Overclock according to Raspberry Pi Pico SDK recommendations.
  set_sys_clock_khz(SYS_CLK_KHZ, true);

  pinMode(LED_BUILTIN, OUTPUT);
  setBootStage(BOOT_STAGE_POWER_ON);
  startBuiltinLedReadyPattern();

  ioBoardController.begin();
  setBootStage(BOOT_STAGE_CORE0_BEGIN);

  // RS485 connection.
  Serial1.end();  // Deactivete UART to empty TX FIFO after reboot
  delayWithBootPattern(5);
  pinMode(RS485_MODE_PIN, OUTPUT);
  digitalWrite(RS485_MODE_PIN, LOW);  // Read mode
  delayWithBootPattern(5);
  // Configure UART1 on GPIO 0 and 1 explicitly seems to fix some strange connection issues.
  // EventDispatcher::ReleaseBusAfterTx() polls uart0's BUSY flag directly to
  // decide when the frame has left the wire. GPIO 0/1 are UART0 on the RP2040,
  // so that only holds while the RS485 UART lives on these pins. Polling the
  // wrong UART would read "idle" immediately and drop the driver mid-frame.
  // The reported board type must be a value the protocol knows, or a host
  // pairing images to boards by name has nothing to match against.
  static_assert(ppuc::v2::BoardTypeName(PPUC_BOARD_TYPE) != nullptr,
                "PPUC_BOARD_TYPE is not a known ppuc::v2::BoardType");

  static_assert(RS485_TX_PIN == 0 && RS485_RX_PIN == 1,
                "RS485 UART must be uart0 on GPIO 0/1, or "
                "ReleaseBusAfterTx() has to be updated with it");

  gpio_set_function(RS485_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(RS485_RX_PIN, GPIO_FUNC_UART);
  Serial1.setTX(RS485_TX_PIN);
  Serial1.setRX(RS485_RX_PIN);
  // The core's default receive buffer is 32 bytes. At 115200 baud that is
  // 2.8ms of traffic, and it is smaller than a single firmware update chunk
  // frame (271 bytes). Anything that blocks this core for longer than that -
  // a burst of switch interrupts on a busy board, a fanout on core 0 - loses
  // incoming bytes before the parser ever sees them. There is no CRC error and
  // no resync when that happens, because the bytes never arrive at all; the
  // board simply misses the frame naming it and stays silent, and the host
  // reports a timeout it cannot explain.
  //
  // Measured on Time Warp: with switches worked hard, the busiest board was
  // selected 113 fewer times than its peers over three minutes while replying
  // to every selection it did see. Sized here to hold the largest frame on the
  // bus several times over, which is ~44ms of slack instead of 2.8ms.
  //
  // Must precede begin(): the buffer is allocated there.
  Serial1.setFIFOSize(512);
  Serial1.begin(ppuc::v2::kBaudRate);
  setBootStage(BOOT_STAGE_UART_READY);
  // Empty RX FIFO after reboot
  while (Serial1.available()) {
    Serial1.read();
  }

  usb_debugging = ioBoardController.isDebug();

  if (usb_debugging) {
    Serial.begin(115200);
    delayWithBootPattern(100);
    setBootStage(BOOT_STAGE_USB_WAIT);
    // Wait for a serial connection of a debugger via USB CDC.
    // The Pico implements USB itself so special care must be taken. Use
    // while(!Serial){} in the setup() code before printing anything so that
    // it waits for the USB connection to be established.
    // https://community.platformio.org/t/serial-monitor-not-working/1512/25
    while (!Serial) {
      updateBuiltinLedBootPattern();
      delay(1);
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
  setBootStage(BOOT_STAGE_CORE1_RESTART);
  rp2040.restartCore1();
}

void setup1() {
  while (!core_0_initialized) {
    updateBuiltinLedBootPattern();
  }

  setBootStage(BOOT_STAGE_CORE1_BEGIN);
  effectsController.begin();
  core_1_initialized = true;

  if (usb_debugging) {
    //delay(10);
    //effectsController.eventDispatcher()->addListener(new CrossLinkDebugger());
  }

  effectsController.eventDispatcher()->setMultiCoreCrossLink(
      ioBoardController.eventDispatcher()->getMultiCoreCrossLink());
  setBootStage(BOOT_STAGE_CROSSLINK_READY);

  effectsController.start();
  setBootStage(BOOT_STAGE_EFFECTS_STARTED);
}

void loop() {
  core_0_loop_running = true;
  if (boot_led_active && core_1_initialized && core_1_loop_running) {
    if (!boot_runtime_stage_entered) {
      setBootStage(BOOT_STAGE_RUNTIME);
      boot_runtime_stage_entered = true;
      boot_runtime_stage_ms = millis();
    } else if ((millis() - boot_runtime_stage_ms) >=
               BOOT_STAGE_RUNTIME_HOLD_MS) {
      boot_led_active = false;
      startBuiltinLedReadyPattern();
    }
  }

  watchdog_ms = millis();
  if (boot_led_active) {
    updateBuiltinLedBootPattern();
  } else if (!ioBoardController.isRunning()) {
    updateBuiltinLedReadyPattern();
  }
  ioBoardController.update();
  lastPoll_ms = ioBoardController.eventDispatcher()->getLastPoll();
}

void loop1() {
  core_1_loop_running = true;
  effectsController.update();
}
