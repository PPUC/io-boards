#include "IOBoardController.h"

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

  if (controllerType == CONTROLLER_16_8_1) {
    initializeBoardIdentity();

    _eventDispatcher->setBoard(boardId);
    _eventDispatcher->setDebug(m_debug);
    _eventDispatcher->setRS485ModePin(RS485_MODE_PIN);
    _eventDispatcher->setCrossLinkSerial(Serial1);
    _multiCoreCrossLink = new MultiCoreCrossLink();
    _eventDispatcher->setMultiCoreCrossLink(_multiCoreCrossLink);
    _pwmDevices = new PwmDevices(_eventDispatcher);
    _switches = new Switches(boardId, _eventDispatcher);
    _switchMatrix = new SwitchMatrix(boardId, _eventDispatcher);
    // Adjust PWM properties if needed.
    analogWriteFreq(500);
    analogWriteResolution(8);
    m_initialized = true;
  }
}

void IOBoardController::update() {
  if (running && (activeSwitches || activeSwitchMatrix)) {
    _eventDispatcher->dispatch(new Event(EVENT_POLL_EVENTS));
  }

  if (running) {
    if (activeSwitches) {
      // nop
    }
    if (activeSwitchMatrix) {
      //switchMatrix()->update();
    }
    if (activePwmDevices) {
      pwmDevices()->update();
    }
  } else {
    if (activePwmDevices) {
      pwmDevices()->off();
    }
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
  port = 0;
  number = 0;
  power = 0;
  rows = 0;
  minPulseTime = 0;
  maxPulseTime = 0;
  holdPower = 0;
  holdPowerActivationTime = 0;
  fastSwitch = 0;
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
        switch (event->key) {
          case CONFIG_TOPIC_ACTIVE_LOW:
            if (event->value) {
              _switchMatrix->setActiveLow();
            }
            break;
          case CONFIG_TOPIC_NUM_ROWS:
            rows = (uint8_t)event->value;
            _switchMatrix->setNumRows(rows);
            _switches->setNumSwitches(MAX_SWITCHES - NUM_COLUMNS - rows);
            break;
          case CONFIG_TOPIC_PORT:
            port = event->value;
            break;
          case CONFIG_TOPIC_NUMBER:
            _switchMatrix->registerSwitch((byte)port, event->value);
            activeSwitchMatrix = true;
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
            _switches->registerSwitch((byte)port, number, event->value);
            activeSwitches = true;
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
            break;
          case CONFIG_TOPIC_TYPE:
            switch (event->value) {
              case PWM_TYPE_SOLENOID:  // Coil
                _pwmDevices->registerSolenoid(
                    (byte)port, number, power, minPulseTime, maxPulseTime,
                    holdPower, holdPowerActivationTime, fastSwitch);
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
                // @todo
                break;
              case PWM_TYPE_SHAKER:  // Shaker
                // Shaker is handled by the EffectController.
                break;
            }
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
