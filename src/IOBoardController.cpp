#include "IOBoardController.h"

IOBoardController::IOBoardController(int cT)
{
    _eventDispatcher = new EventDispatcher();
    _eventDispatcher->addListener(this, EVENT_CONFIGURATION);
    _eventDispatcher->addListener(this, EVENT_PING);
    _eventDispatcher->addListener(this, EVENT_RESET);

    controllerType = cT;

    if (controllerType == CONTROLLER_16_8_1)
    {
        // Read bordID. Ideal value at 10bit resolution: (DIP+1)*1023*2/35 -> 58.46 to 935.3
        boardId = 16 - ((int)((analogRead(28) + 29.23) / 58.46));
        _eventDispatcher->setRS485ModePin(2);
        _eventDispatcher->setBoard(boardId);
        _eventDispatcher->setCrossLinkSerial(Serial1);
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
        _multiCoreCrossLink = new MultiCoreCrossLink();
        _eventDispatcher->setMultiCoreCrossLink(_multiCoreCrossLink);
#endif
        _pwmDevices = new PwmDevices(_eventDispatcher);
        _switches = new Switches(boardId, _eventDispatcher);
        _switchMatrix = new SwitchMatrix(boardId, _eventDispatcher);
    }
}

void IOBoardController::update()
{
    if (activeSwitches)
    {
        switches()->update();
    }
    if (activeSwitchMatrix)
    {
        switchMatrix()->update();
    }
    if (activePwmDevices)
    {
        pwmDevices()->update();
    }

    eventDispatcher()->update();
}

void IOBoardController::handleEvent(Event *event)
{
    switch (event->sourceId)
    {
    case EVENT_PING:
        _eventDispatcher->dispatch(new Event(EVENT_PONG, 1, boardId));
        break;

    case EVENT_RESET:
        // @todo clear all configurations or reboot the device.
        break;
    }
}

void IOBoardController::handleEvent(ConfigEvent *event)
{
    if (event->boardId == boardId)
    {
        switch (event->topic)
        {
        case CONFIG_TOPIC_SWITCHES:
            switch (event->key)
            {
            case CONFIG_TOPIC_PORT:
                port = event->value;
                break;
            case CONFIG_TOPIC_NUMBER:
                _switches->registerSwitch((byte)port, event->value);
                activeSwitches = true;
                break;
            }
            break;

        case CONFIG_TOPIC_SWITCH_MATRIX:
            switch (event->key)
            {
            case CONFIG_TOPIC_ACTIVE_LOW:
                if (event->value)
                {
                    _switchMatrix->setActiveLow();
                }
                break;
            case CONFIG_TOPIC_MAX_PULSE_TIME:
                _switchMatrix->setPulseTime((byte)event->value);
                break;
            case CONFIG_TOPIC_TYPE:
                type = event->value;
                number = 0;
                port = 0;
                break;
            case CONFIG_TOPIC_NUMBER:
                number = event->value;
                break;
            case CONFIG_TOPIC_PORT:
                port = event->value;
                if (MATRIX_TYPE_COLUMN == type)
                {
                    _switchMatrix->registerColumn(port, number);
                }
                else
                {
                    _switchMatrix->registerRow(port, number);
                }
                activeSwitchMatrix = true;
                break;
            }

            break;

        case CONFIG_TOPIC_PWM:
            switch (event->key)
            {
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
                switch (event->value)
                {
                case PWM_TYPE_SOLENOID: // Coil
                    _pwmDevices->registerSolenoid((byte)port, number, power, minPulseTime, maxPulseTime, holdPower, holdPowerActivationTime, fastSwitch);
                    activePwmDevices = true;
                    break;
                case PWM_TYPE_FLASHER: // Flasher
                    _pwmDevices->registerFlasher((byte)port, number, power);
                    activePwmDevices = true;
                    break;
                case PWM_TYPE_LAMP: // Lamp
                    _pwmDevices->registerLamp((byte)port, number, power);
                    activePwmDevices = true;
                    break;
                }
                break;
            }
            break;
        }
    }
}

PwmDevices *IOBoardController::pwmDevices()
{
    return _pwmDevices;
}

Switches *IOBoardController::switches()
{
    return _switches;
}

SwitchMatrix *IOBoardController::switchMatrix()
{
    return _switchMatrix;
}

EventDispatcher *IOBoardController::eventDispatcher()
{
    return _eventDispatcher;
}
