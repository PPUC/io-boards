#include "EffectsController.h"

// see https://forum.arduino.cc/index.php?topic=398610.0
EffectsController *EffectsController::effectsControllerInstance = NULL;

EventDispatcher *EffectsController::eventDispatcher()
{
    return _eventDispatcher;
}

LedBuiltInDevice *EffectsController::ledBuiltInDevice()
{
    return _ledBuiltInDevice;
}

NullDevice *EffectsController::nullDevice()
{
    return _nullDevice;
}

WavePWMDevice *EffectsController::shakerPWMDevice()
{
    return _shakerPWMDevice;
}

WavePWMDevice *EffectsController::ledPWMDevice()
{
    return _ledPWMDevice;
}

RgbStripDevice *EffectsController::rgbStripDevice()
{
    return _rgbStripeDevice;
}

WS2812FXDevice *EffectsController::ws2812FXDevice(int port)
{
    return ws2812FXDevices[--port][0];
}

CombinedGiAndLightMatrixWS2812FXDevice *EffectsController::giAndLightMatrix(int port)
{
    return (CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevice(port);
}

CombinedGiAndLightMatrixWS2812FXDevice *EffectsController::createCombinedGiAndLightMatrixWs2812FXDevice(int port)
{
    WS2812FXDevice *ws2812FXDevice = ws2812FXDevices[--port][0];

    CombinedGiAndLightMatrixWS2812FXDevice *giAndLightMatrix = new CombinedGiAndLightMatrixWS2812FXDevice(
        ws2812FXDevice->getWS2812FX(),
        ws2812FXDevice->getFirstLED(),
        ws2812FXDevice->getlastLED(),
        ws2812FXDevice->getFirstSegment(),
        ws2812FXDevice->getLastSegment());

    giAndLightMatrix->off();

    ws2812FXDevices[port][0] = giAndLightMatrix;
    delete ws2812FXDevice;

    _eventDispatcher->addListener(giAndLightMatrix, EVENT_SOURCE_GI);
    _eventDispatcher->addListener(giAndLightMatrix, EVENT_SOURCE_LIGHT);

    return giAndLightMatrix;
}

WS2812FXDevice *EffectsController::createWS2812FXDevice(int port, int number, int segments, int firstLED, int lastLED)
{
    --port;

    if (number == 0)
    {
        ws2812FXDevices[port][0]->_reduceLEDs(lastLED, segments - 1);
    }
    else
    {
        int firstSegment = ws2812FXDevices[port][number - 1]->getLastSegment() + 1;

        ws2812FXDevices[port][number] = new WS2812FXDevice(
            ws2812FXDevices[port][0]->getWS2812FX(),
            firstLED,
            lastLED,
            firstSegment,
            firstSegment + segments - 1);

        ++ws2812FXDeviceCounters[port];
    }

    return ws2812FXDevices[port][number];
}

WS2812FXDevice *EffectsController::ws2812FXDevice(int port, int number)
{
    return ws2812FXDevices[--port][number];
}

GeneralIlluminationWPC *EffectsController::generalIllumintationWPC()
{
    return _generalIllumintationWPC;
}

void EffectsController::addEffect(Effect *effect, EffectDevice *device, Event *event, int priority, int repeat, int mode)
{
    addEffect(new EffectContainer(effect, device, event, priority, repeat, mode));
}

void EffectsController::addEffect(EffectContainer *container)
{
    container->effect->setEventDispatcher(this->eventDispatcher());
    container->effect->setDevice(container->device);
    stackEffectContainers[++stackCounter] = container;
}

void EffectsController::attachBrightnessControl(byte port, byte poti)
{
    brightnessControl[--port] = poti;
}

void EffectsController::setBrightness(byte port, byte brightness)
{
    ws2812FXDevices[--port][0]->setBrightness(brightness);
    ws2812FXbrightness[port] = brightness;
}

void EffectsController::handleEvent(Event *event)
{
    for (int i = 0; i <= stackCounter; i++)
    {
        if (
            event->sourceId == stackEffectContainers[i]->event->sourceId &&
            event->eventId == stackEffectContainers[i]->event->eventId &&
            event->value == stackEffectContainers[i]->event->value &&
            (mode == stackEffectContainers[i]->mode ||
             -1 == stackEffectContainers[i]->mode // -1 means any mode
             ))
        {
            for (int k = 0; k <= stackCounter; k++)
            {
                if (
                    stackEffectContainers[i]->device == stackEffectContainers[k]->device &&
                    stackEffectContainers[k]->effect->isRunning())
                {
                    if (stackEffectContainers[i]->priority > stackEffectContainers[k]->priority)
                    {
                        stackEffectContainers[k]->effect->terminate();
                        stackEffectContainers[i]->effect->start(stackEffectContainers[i]->repeat);
                    }
                    break;
                }
                if (k == stackCounter)
                {
                    stackEffectContainers[i]->effect->start(stackEffectContainers[i]->repeat);
                }
            }
        }
    }
}

void EffectsController::handleEvent(ConfigEvent *event)
{
    if (event->boardId == boardId)
    {
        switch (event->topic)
        {
        case CONFIG_TOPIC_PLATFORM:
            platform = event->value;
            break;

        case CONFIG_TOPIC_LED_STRING:
            switch (event->key)
            {
            case CONFIG_TOPIC_PORT:
                config_port = event->value;
                config_type = 0;
                config_amount = 0;
                config_afterGlow = 0;
                break;
            case CONFIG_TOPIC_TYPE:
                config_type = event->value;
                break;
            case CONFIG_TOPIC_AMOUNT_LEDS:
                config_amount = event->value;
                break;
            case CONFIG_TOPIC_AFTER_GLOW:
                config_afterGlow = event->value;
                break;
            case CONFIG_TOPIC_LIGHT_UP:
                config_heatUp = event->value;

                if (!ws2812FXDevices[0][0])
                {
                    ws2812FXDevices[0][0] = new CombinedGiAndLightMatrixWS2812FXDevice(
                        new WS2812FX(config_amount, config_port, config_type),
                        0,
                        config_amount - 1,
                        0,
                        0);
                    ws2812FXDevices[0][0]->getWS2812FX()->init();
                    ws2812FXDeviceCounters[0] = 1;

                    // Brightness might be overwritten later.
                    // ws2812FXDevices[0][0]->setBrightness(WS2812FX_BRIGHTNESS);
                    ws2812FXDevices[0][0]->off();
                    ws2812FXstates[0] = true;

                    _eventDispatcher->addListener((EventListener *)ws2812FXDevices[0][0], EVENT_SOURCE_GI);
                    _eventDispatcher->addListener((EventListener *)ws2812FXDevices[0][0], EVENT_SOURCE_SOLENOID);
                    _eventDispatcher->addListener((EventListener *)ws2812FXDevices[0][0], EVENT_SOURCE_LIGHT);
                }

                break;
            }
            break;

        case CONFIG_TOPIC_LAMPS:
            if (ws2812FXDevices[0][0])
            {
                switch (event->key)
                {
                case CONFIG_TOPIC_PORT:
                    config_port = event->value;
                    config_type = 0;
                    config_number = 0;
                    config_ledNumber = 0;
                    config_brightness = 0;
                    config_color = 0;
                    break;
                case CONFIG_TOPIC_TYPE:
                    config_type = event->value;
                    break;
                case CONFIG_TOPIC_NUMBER:
                    config_number = event->value;
                    break;
                case CONFIG_TOPIC_LED_NUMBER:
                    config_ledNumber = event->value;
                    break;
                case CONFIG_TOPIC_BRIGHTNESS:
                    config_brightness = event->value;
                    break;
                case CONFIG_TOPIC_COLOR:
                    config_color = event->value;
                    switch (config_type)
                    {
                    case LED_TYPE_GI:
                        ((CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevices[0][0])->assignLedToGiString(config_number, config_ledNumber, config_color);
                        break;
                    case LED_TYPE_LAMP:
                        if (platform == PLATFORM_WPC)
                        {
                            ((CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevices[0][0])->assignLedToLightMatrixWPC(config_number, config_ledNumber, config_color);
                        }
                        else
                        {
                            ((CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevices[0][0])->assignLedToLightMatrix(config_number, config_ledNumber, config_color);
                        }
                        break;
                    case LED_TYPE_FLASHER:
                        ((CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevices[0][0])->assignLedToFlasher(config_number, config_ledNumber, config_color);
                        break;
                    }
                    break;
                }
                break;
            }
        }
    }
}

void EffectsController::update()
{
    if (controllerType == CONTROLLER_MEGA_ALL_INPUT)
    {
        _testButtons->update();
    }

    if (platform == PLATFORM_WPC && controllerType != CONTROLLER_16_8_1)
    {
        _generalIllumintationWPC->update();
    }

    _eventDispatcher->update();

    for (int i = 0; i <= stackCounter; i++)
    {
        if (stackEffectContainers[i]->effect->isRunning())
        {
            stackEffectContainers[i]->effect->updateMillis();
            stackEffectContainers[i]->effect->update();
        }
    }

    if (millis() - ws2812UpdateInterval > UPDATE_INTERVAL_WS2812FX_EFFECTS)
    {
        // Updating the LEDs too fast leads to undefined behavior. Just update effects every 3ms.
        ws2812UpdateInterval = millis();

        for (int i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++)
        {
            if (ws2812FXstates[i])
            {
                if (ws2812FXrunning[i])
                {
                    ws2812FXDevices[i][0]->getWS2812FX()->service();

                    bool stop = true;
                    for (int k = 0; k < ws2812FXDeviceCounters[i]; k++)
                    {
                        stop &= ws2812FXDevices[i][k]->isStopped();
                    }

                    if (stop)
                    {
                        ws2812FXDevices[i][0]->getWS2812FX()->stop();
                        ws2812FXrunning[i] = false;
                    }
                }
                else
                {
                    bool start = false;
                    for (int k = 0; k < ws2812FXDeviceCounters[i]; k++)
                    {
                        start |= !ws2812FXDevices[i][k]->isStopped();
                    }

                    if (start)
                    {
                        ws2812FXDevices[i][0]->getWS2812FX()->start();
                        ws2812FXrunning[i] = true;
                        ws2812FXDevices[i][0]->getWS2812FX()->service();
                    }
                }
            }
        }
    }

    if (millis() - ws2812AfterGlowUpdateInterval > UPDATE_INTERVAL_WS2812FX_AFTERGLOW)
    {
        // Updating the LEDs too fast leads to undefined behavior. Just update every 3ms.
        ws2812AfterGlowUpdateInterval = millis();
        for (int i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++)
        {
            if (ws2812FXstates[i] && ws2812FXDevices[i][0]->hasAfterGlowSupport() && !ws2812FXrunning[i])
            {
                // No other effect is running, handle after glow effect.
                ((CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevices[i][0])->updateAfterGlow();
                ws2812FXDevices[i][0]->getWS2812FX()->show();
            }
        }
    }

    if (brightnessControlBasePin > 0)
    {
        if (millis() - brightnessUpdateInterval > UPDATE_INTERVAL_WS2812FX_BRIGHTNESS)
        {
            // Don't update the brightness too often.
            brightnessUpdateInterval = millis();
            for (byte i = 0; i < PPUC_MAX_BRIGHTNESS_CONTROLS; i++)
            {
                brightnessControlReads[i] = analogRead(brightnessControlBasePin + i) / 4;
            }
            for (byte i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++)
            {
                if (brightnessControl[i] > 0)
                {
                    setBrightness(i + 1, brightnessControlReads[brightnessControl[i - 1]]);
                }
            }
        }
    }
}

void EffectsController::start()
{
    for (int i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++)
    {
        if (ws2812FXbrightness[i] == 0)
        {
            //setBrightness(i + 1, WS2812FX_BRIGHTNESS);
        }
    }

    _eventDispatcher->dispatch(
        new Event(EVENT_SOURCE_EFFECT, 1, 255));
}
