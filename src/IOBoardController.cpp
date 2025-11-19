#include "IOBoardController.h"

#include "EventDispatcher/CrossLinkDebugger.h"

IOBoardController::IOBoardController(int cT) {
  _eventDispatcher = new EventDispatcher();
  _eventDispatcher->addListener(this, EVENT_CONFIGURATION);
  _eventDispatcher->addListener(this, EVENT_PING);
  _eventDispatcher->addListener(this, EVENT_RUN);
  _eventDispatcher->addListener(this, EVENT_RESET);

  controllerType = cT;

  if (controllerType == CONTROLLER_16_8_1) {
    // Read bordID. Ideal value at 10bit resolution: (DIP+1)*1023*2/35 -> 58.46
    // to 935.3
    boardId = 16 - ((int)((analogRead(28) + 29.23) / 58.46));
    _eventDispatcher->setRS485ModePin(2);
    _eventDispatcher->setBoard(boardId);
    _eventDispatcher->setCrossLinkSerial(Serial1);
    _multiCoreCrossLink = new MultiCoreCrossLink();
    _eventDispatcher->setMultiCoreCrossLink(_multiCoreCrossLink);
    _pwmDevices = new PwmDevices(_eventDispatcher);
    _switches = new Switches(boardId, _eventDispatcher);
    _switchMatrix = new SwitchMatrix(boardId, _eventDispatcher);
    // Adjust PWM properties if needed.
    analogWriteFreq(500);
    analogWriteResolution(8);
  }
}

void IOBoardController::update() {
  if (running) {
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
      rp2040.reboot();
    } else {
      resetTimer = 0;
      CrossLinkDebugger::debug(
          "Skipped reset to keep USB debugging connection alive.");
    }
  }

  eventDispatcher()->update();
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
      // Clear all configurations or reboot the device.
      _pwmDevices->reset();

      // Issue a delayed reset of the board.
      // Core 1 should have enough time to turn off it's devices.
      resetTimer = millis() + WAIT_FOR_EFFECT_CONTROLLER_RESET;

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
            break;
        }
        break;

      case CONFIG_TOPIC_SWITCHES:
        switch (event->key) {
          case CONFIG_TOPIC_PORT:
            port = event->value;
            break;
          case CONFIG_TOPIC_NUMBER:
            _switches->registerSwitch((byte)port, event->value);
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
