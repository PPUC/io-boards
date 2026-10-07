#include "IOBoardController.h"

#include "PPUC.h"
#include "SafeOff.h"

#include "EventDispatcher/CrossLinkDebugger.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#define SWITCH_DEBOUNCE 10

namespace {
constexpr uint8_t kBoardSelectorSamples = 8;
constexpr uint32_t kBoardSelectorSampleDelayMs = 2;

uint8_t decodeBoardSelectorValue(int raw) {
  return static_cast<uint8_t>(16 - static_cast<int>((raw + 29.23) / 58.46));
}

// Board-local geometry lives in PPUCBoardTypes.h so adding a board means
// editing one table rather than finding every switch statement.
constexpr ppuc::board::Profile kProfile = ppuc::board::self();
static_assert(kProfile.type != ppuc::v2::kBoardTypeUnknown,
              "PPUC_BOARD_TYPE has no profile in PPUCBoardTypes.h");

[[noreturn]] void performBoardReboot() {
  // Stop the second core first so the chip does not reboot with core 1 still
  // executing stale firmware state while core 0 is tearing down UART/RS485.
  multicore_reset_core1();
  delay(1);

  Serial1.end();
  delay(5);
  pinMode(RS485_MODE_PIN, OUTPUT);
  digitalWrite(RS485_MODE_PIN, LOW);
  delay(5);

  // Reset can be triggered while UART RX/TX state is still active. Disable
  // interrupts and give the serial hardware a brief moment to settle before
  // arming the watchdog reboot.
  (void)save_and_disable_interrupts();
  busy_wait_us_32(10000);
  watchdog_reboot(0, 0, 0);
  while (true) {
  }
}
}

IOBoardController::IOBoardController(int cT) {
  _eventDispatcher = new EventDispatcher();
  _eventDispatcher->addListener(this, EVENT_CONFIGURATION);
  _eventDispatcher->addListener(this, EVENT_PING);
  _eventDispatcher->addListener(this, EVENT_RUN);
  _eventDispatcher->addListener(this, EVENT_RESET);

  controllerType = cT;
  _pwmDevices = nullptr;
  _switches = nullptr;
  _switchMatrix = nullptr;
  _strobedSwitchMatrix = nullptr;
  _lampMatrix = nullptr;
  _multiCoreCrossLink = nullptr;
  boardId = 255;
}

int IOBoardController::readBoardSelectorRaw() const {
  uint8_t votes[16] = {0};
  for (uint8_t i = 0; i < kBoardSelectorSamples; ++i) {
    const uint8_t decoded = decodeBoardSelectorValue(analogRead(28));
    if (decoded < 16) {
      votes[decoded]++;
    }
    delay(kBoardSelectorSampleDelayMs);
  }

  uint8_t bestValue = 0;
  uint8_t bestVotes = 0;
  for (uint8_t value = 0; value < 16; ++value) {
    if (votes[value] > bestVotes) {
      bestVotes = votes[value];
      bestValue = value;
    }
  }

  return bestValue;
}

void IOBoardController::initializeBoardIdentity() {
  // Let the board-id resistor ladder settle after power-on or reboot before
  // deriving board/debug mode from the ADC reading.
  delay(2);

  boardId = static_cast<byte>(readBoardSelectorRaw());
  m_debug = (boardId & 0b1000) != 0;
  if (m_debug) {
    boardId -= 8;
  }
}

void IOBoardController::begin() {
  if (m_initialized) {
    return;
  }

  {
    initializeBoardIdentity();

    _eventDispatcher->setBoard(boardId);
    _eventDispatcher->setDebug(m_debug);
    _eventDispatcher->setRS485ModePin(RS485_MODE_PIN);
    _eventDispatcher->setCrossLinkSerial(Serial1);
    _multiCoreCrossLink = new MultiCoreCrossLink();
    _eventDispatcher->setMultiCoreCrossLink(_multiCoreCrossLink);

    // Build only the subsystems this board declares. Everything used to be
    // built on every board, so an Out_8x10 carried a coil stage and a switch
    // reader for pins that are lamp drivers there. A subsystem that does not
    // exist cannot be configured onto the wrong pin.
    if (kProfile.has(ppuc::board::kCapPwmOutputs)) {
      _pwmDevices = new PwmDevices(_eventDispatcher);
    }
    if (kProfile.has(ppuc::board::kCapDedicatedSwitches)) {
      _switches = new Switches(boardId, _eventDispatcher);
    }
    if (kProfile.has(ppuc::board::kCapSwitchMatrix)) {
      _switchMatrix =
          new SwitchMatrix(boardId, _eventDispatcher, kProfile.matrix);
    }
    if (kProfile.has(ppuc::board::kCapStrobedSwitchMatrix)) {
      _strobedSwitchMatrix = new StrobedSwitchMatrix(
          boardId, _eventDispatcher, kProfile.strobedMatrix);
    }
    if (kProfile.has(ppuc::board::kCapLampMatrix)) {
      _lampMatrix = new LampMatrix(_eventDispatcher, kProfile);
    }
    // Adjust PWM properties if needed.
    analogWriteFreq(500);
    analogWriteResolution(8);
    m_initialized = true;
  }
}

void IOBoardController::update() {
  if (running && (activeSwitches || activeSwitchMatrix)) {
    _eventDispatcher->dispatch(new Event(EVENT_POLL_EVENTS));
    // Flush local switch edges before board-local PWM processing so fast-flip
    // coils react to the freshest possible switch state in the same loop.
    _eventDispatcher->update();
  }

  if (running) {
    if (activeSwitches) {
      // nop
    }
    // SwitchMatrix has no update(); like Switches it is driven entirely by
    // EVENT_POLL_EVENTS dispatched above. The commented-out call here named a
    // method that never existed and read as though the matrix were disabled.
    if (activePwmDevices) {
      pwmDevices()->update();
    }
  } else {
    if (activePwmDevices) {
      pwmDevices()->off();
    }
  }

  if (_lampMatrix) {
    _lampMatrix->setRunning(running && activeLampMatrix);
  }

  if (resetTimer > 0 && resetTimer < millis()) {
    if (!m_debug) {
      performBoardReboot();
    } else {
      resetTimer = 0;
      CrossLinkDebugger::debug(
          "Skipped reset to keep USB debugging connection alive.");
    }
  }

  eventDispatcher()->update();
}

void IOBoardController::clearConfiguredState() {
  running = false;
  activePwmDevices = false;
  activeSwitches = false;
  activeSwitchMatrix = false;
  activeLampMatrix = false;
  pwmPinsInUse = 0;
  port = 0;
  number = 0;
  power = 0;
  rows = 0;
  minPulseTime = 0;
  maxPulseTime = 0;
  holdPower = 0;
  holdPowerActivationTime = 0;
  fastSwitch = 0;
  stopSwitch1 = 0;
  stopSwitch2 = 0;
  type = 0;
  resetTimer = 0;

  if (_pwmDevices) {
    _pwmDevices->off();
    _pwmDevices->reset();
    _pwmDevices->resetHighPowerConfig();
  }
  if (_switches) {
    _switches->resetConfig();
  }
  if (_switchMatrix) {
    _switchMatrix->resetConfig();
  }
  if (_strobedSwitchMatrix) {
    _strobedSwitchMatrix->resetConfig();
  }
  if (_lampMatrix) {
    _lampMatrix->resetConfig();
  }
}

// The host asked for something this board cannot do: a device on a pin it
// does not have, or a subsystem it does not carry.
//
// The request is dropped and EVENT_ERROR fast-blinks the on-board LED. The
// host validates the same things against the same table before it sends
// anything, so getting here means the board on this address is not the type
// the configuration was written for.
void IOBoardController::reportConfigError() {
  _eventDispatcher->dispatch(new Event(EVENT_ERROR));
}

void IOBoardController::handleSwitchMatrixConfig(ConfigEvent *event) {
  if (_strobedSwitchMatrix) {
    switch (event->key) {
      case CONFIG_TOPIC_ACTIVE_LOW:
        if (event->value) {
          _strobedSwitchMatrix->setActiveLow();
        }
        break;
      case CONFIG_TOPIC_NUM_ROWS:
        rows = (uint8_t)event->value;
        if (!_strobedSwitchMatrix->setNumRows(rows)) {
          reportConfigError();
        }
        break;
      case CONFIG_TOPIC_PORT:
        port = event->value;
        break;
      case CONFIG_TOPIC_NUMBER:
        if (_strobedSwitchMatrix->registerSwitch((byte)port, event->value)) {
          activeSwitchMatrix = true;
          // All sixteen inputs are returns from here on, whatever the row
          // count says: the scan samples every one of them.
          if (_switches) {
            _switches->setNumSwitches(0);
          }
        } else {
          reportConfigError();
        }
        break;
    }
    return;
  }

  if (!_switchMatrix) {
    reportConfigError();
    return;
  }

  switch (event->key) {
    case CONFIG_TOPIC_ACTIVE_LOW:
      if (event->value) {
        _switchMatrix->setActiveLow();
      }
      break;
    case CONFIG_TOPIC_NUM_ROWS:
      rows = (uint8_t)event->value;
      if (_switchMatrix->setNumRows(rows)) {
        const uint8_t matrixPinsUsed = _switchMatrix->matrixPinsUsed();
        _switches->setNumSwitches(
            matrixPinsUsed < MAX_SWITCHES ? MAX_SWITCHES - matrixPinsUsed : 0);
      }
      break;
    case CONFIG_TOPIC_PORT:
      port = event->value;
      break;
    case CONFIG_TOPIC_NUMBER:
      _switchMatrix->registerSwitch((byte)port, event->value);
      activeSwitchMatrix = true;
      break;
  }
}

// Registers the output the last CONFIG_TOPIC_PWM block described.
void IOBoardController::registerPwmOutput(byte pwmType) {
  if (_lampMatrix) {
    // This board's outputs are lamp drivers with no PWM behind them. A lamp
    // wired to a single output is welcome; a coil is not.
    if (pwmType == PWM_TYPE_LAMP &&
        _lampMatrix->registerDirect((byte)port, number)) {
      activeLampMatrix = true;
    } else {
      reportConfigError();
    }
    return;
  }

  if (!_pwmDevices || !kProfile.allowsPwm(port)) {
    reportConfigError();
    return;
  }
  if (_strobedSwitchMatrix && _strobedSwitchMatrix->isActive() &&
      kProfile.isStrobePin(port)) {
    // The matrix scan owns this pin.
    reportConfigError();
    return;
  }
  for (uint8_t pin = 0; pin < 32; pin++) {
    if ((pwmPinsInUse & (1u << pin)) != 0 &&
        ppuc::board::sharesPwmChannel(pin, port)) {
      // Whatever is written to one of these two pins comes out of both.
      reportConfigError();
      return;
    }
  }
  pwmPinsInUse |= ppuc::board::pinBit(port);

  switch (pwmType) {
    case PWM_TYPE_SOLENOID:  // Coil
      _pwmDevices->registerSolenoid((byte)port, number, power, minPulseTime,
                                    maxPulseTime, holdPower,
                                    holdPowerActivationTime, fastSwitch,
                                    stopSwitch1, stopSwitch2);
      activePwmDevices = true;
      break;
    case PWM_TYPE_FLASHER:  // Flasher
      _pwmDevices->registerFlasher((byte)port, number, power);
      activePwmDevices = true;
      break;
    case PWM_TYPE_LAMP:  // Lamp
      _pwmDevices->registerLamp((byte)port, number, power);
      activePwmDevices = true;
      break;
    case PWM_TYPE_MOTOR:  // Motor
      // Driven exactly like a coil - it is a PWM output with a power
      // and a pulse time - but registered under its own type so its
      // end-of-travel switches can stop it.
      _pwmDevices->registerSolenoid((byte)port, number, power, minPulseTime,
                                    maxPulseTime, holdPower,
                                    holdPowerActivationTime, fastSwitch,
                                    stopSwitch1, stopSwitch2, PWM_TYPE_MOTOR);
      activePwmDevices = true;
      break;
    case PWM_TYPE_SHAKER:  // Shaker
      // Shaker is handled by the EffectController. Its pin is recorded
      // above so nothing else lands on the same PWM channel.
      break;
  }
}

void IOBoardController::handleEvent(Event *event) {
  switch (event->sourceId) {
    case EVENT_PING:
      // In case that serial debugging is active, send 99 as PING response,
      // otherwise 1.
      _eventDispatcher->dispatch(
          new Event(EVENT_PONG, m_debug ? 99 : 1, boardId));
      break;

    case EVENT_RUN:
      running = (bool)event->value;
      break;

    case EVENT_RESET:
      clearConfiguredState();

      // Issue a delayed reset of the board.
      // Core 1 should have enough time to turn off it's devices.
      resetTimer = millis() + WAIT_FOR_EFFECT_CONTROLLER_RESET;

      break;

    case EVENT_RESTART:
      clearConfiguredState();
      break;
  }
}

void IOBoardController::handleEvent(ConfigEvent *event) {
  if (event->boardId == boardId) {
    switch (event->topic) {
      case CONFIG_TOPIC_SWITCH_MATRIX:
        handleSwitchMatrixConfig(event);
        break;

      case CONFIG_TOPIC_LAMP_MATRIX:
        if (!_lampMatrix) {
          reportConfigError();
          break;
        }
        switch (event->key) {
          case CONFIG_TOPIC_NUM_ROWS:
            if (!_lampMatrix->setNumRows((uint8_t)event->value)) {
              reportConfigError();
            }
            break;
          case CONFIG_TOPIC_PORT:
            port = event->value;
            break;
          case CONFIG_TOPIC_NUMBER:
            if (_lampMatrix->registerLamp((byte)port, event->value)) {
              activeLampMatrix = true;
            } else {
              reportConfigError();
            }
            break;
        }
        break;

      case CONFIG_TOPIC_SWITCH_CHAIN:
        if (event->key == CONFIG_TOPIC_NEXT_BOARD) {
          _eventDispatcher->setNextSwitchBoard((byte)event->value);
        } else if (event->key == CONFIG_TOPIC_SWITCH_REPLY_DELAY_US) {
          _eventDispatcher->setSwitchReplyDelayUs(event->value);
        }
        break;

      case CONFIG_TOPIC_SWITCHES:
        switch (event->key) {
          case CONFIG_TOPIC_PORT:
            port = event->value;
            break;
          case CONFIG_TOPIC_NUMBER:
            number = event->value;
            break;
          case CONFIG_TOPIC_DEBOUNCE_TIME:
            if (_switches && kProfile.allowsSwitch(port)) {
              _switches->registerSwitch((byte)port, number, event->value);
              activeSwitches = true;
            } else {
              reportConfigError();
            }
            break;
          case CONFIG_TOPIC_MODE:
            if (_switches) {
              _switches->setDebounceMode(number, event->value);
            }
            break;
        }
        break;

      case CONFIG_TOPIC_PWM:
        switch (event->key) {
          case CONFIG_TOPIC_PORT:
            port = event->value;
            number = 0;
            power = 0;
            minPulseTime = 0;
            maxPulseTime = 0;
            holdPower = 0;
            holdPowerActivationTime = 0;
            fastSwitch = 0;
            stopSwitch1 = 0;
            stopSwitch2 = 0;
            break;
          case CONFIG_TOPIC_NUMBER:
            number = event->value;
            break;
          case CONFIG_TOPIC_POWER:
            power = event->value;
            break;
          case CONFIG_TOPIC_MIN_PULSE_TIME:
            minPulseTime = event->value;
            break;
          case CONFIG_TOPIC_MAX_PULSE_TIME:
            maxPulseTime = event->value;
            break;
          case CONFIG_TOPIC_HOLD_POWER:
            holdPower = event->value;
            break;
          case CONFIG_TOPIC_HOLD_POWER_ACTIVATION_TIME:
            holdPowerActivationTime = event->value;
            break;
          case CONFIG_TOPIC_FAST_SWITCH:
            fastSwitch = event->value;
            if (_switches) {
              _switches->markLocalFastSwitch(fastSwitch);
            }
            break;
          case CONFIG_TOPIC_STOP_SWITCH:
            stopSwitch1 = event->value;
            // Marked local so it reaches the outputs the moment it closes,
            // rather than on the next queue drain. A switch that stops a motor
            // is worth nothing if it arrives late.
            if (_switches) {
              _switches->markLocalFastSwitch(stopSwitch1);
            }
            break;
          case CONFIG_TOPIC_STOP_SWITCH_2:
            stopSwitch2 = event->value;
            if (_switches) {
              _switches->markLocalFastSwitch(stopSwitch2);
            }
            break;
          case CONFIG_TOPIC_TYPE:
            registerPwmOutput((byte)event->value);
            break;
        }
        break;
    }
  }
}

PwmDevices *IOBoardController::pwmDevices() { return _pwmDevices; }

Switches *IOBoardController::switches() { return _switches; }

SwitchMatrix *IOBoardController::switchMatrix() { return _switchMatrix; }

EventDispatcher *IOBoardController::eventDispatcher() {
  return _eventDispatcher;
}
