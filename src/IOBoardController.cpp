#include "IOBoardController.h"

IOBoardController::IOBoardController(int controllerType) {
    _eventDispatcher = new EventDispatcher();
    _eventDispatcher->addListener(this, EVENT_CONFIGURATION);

    if (controllerType == CONTROLLER_16_8_1) {
        // Turn on the LED
        pinMode(25, OUTPUT);
        digitalWrite(25, HIGH);

        // Read bordID. The read value is between 60 and 940.
        boardId = 16 - ((int) ((analogRead(28) + 20) / 60));

        #if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
        Serial1.setTX(0);
        Serial1.setRX(1);
        Serial1.setFIFOSize(128); // @todo find the right size.
        Serial1.begin(115200);
        _eventDispatcher->setRS485ModePin(2);
        _eventDispatcher->setCrossLinkSerial(Serial1);
        _multiCoreCrossLink = new MultiCoreCrossLink();
        _eventDispatcher->setMultiCoreCrossLink(_multiCoreCrossLink);
        #endif

        _pwmDevices = new PwmDevices(_eventDispatcher);
        _switches = new Switches(boardId, _eventDispatcher);
    } else {
        Serial.print("Unsupported Input Controller: ");
        Serial.println(controllerType);
    }
}

void IOBoardController::update() {
    switches()->update();
    pwmDevices()->update();
    eventDispatcher()->update();
}

void IOBoardController::handleEvent(Event* event) {
    switch (event->sourceId) {
        case EVENT_PING:
            _eventDispatcher->dispatch(new Event(EVENT_PONG, 0, boardId));
            break;
    }
}

void IOBoardController::handleEvent(ConfigEvent* event) {
    if (event->boardId == boardId) {
        switch (event->topic) {
            case CONFIG_TOPIC_SWITCHES:
                switch (event->key) {
                    case CONFIG_TOPIC_PORT:
                        port = event->value;
                        break;
                    case CONFIG_TOPIC_NUMBER:
                        _switches->registerSwitch((byte) port, event->value);
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
                            case PWM_TYPE_SOLENOID: // Coil
                                _pwmDevices->registerSolenoid((byte) port, number, power, minPulseTime, maxPulseTime, holdPower, holdPowerActivationTime, fastSwitch);
                                break;
                            case PWM_TYPE_FLASHER: // Flasher
                                _pwmDevices->registerFlasher((byte) port, number, power);
                                break;
                            case PWM_TYPE_LAMP: // Lamp
                                _pwmDevices->registerLamp((byte) port, number, power);
                                break;
                        }
                        break;
                }
                break;
        }
    }
}

PwmDevices* IOBoardController::pwmDevices() {
    return _pwmDevices;
}

Switches* IOBoardController::switches() {
    return _switches;
}

EventDispatcher* IOBoardController::eventDispatcher() {
    return _eventDispatcher;
}

