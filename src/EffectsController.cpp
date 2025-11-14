#include "EffectsController.h"

// see https://forum.arduino.cc/index.php?topic=398610.0
EffectsController *EffectsController::effectsControllerInstance = NULL;

EventDispatcher *EffectsController::eventDispatcher() {
  return _eventDispatcher;
}

LedBuiltInDevice *EffectsController::ledBuiltInDevice() {
  return _ledBuiltInDevice;
}

NullDevice *EffectsController::nullDevice() { return _nullDevice; }

WavePWMDevice *EffectsController::shakerPWMDevice() { return _shakerPWMDevice; }

WavePWMDevice *EffectsController::ledPWMDevice() { return _ledPWMDevice; }

RgbStripDevice *EffectsController::rgbStripDevice() { return _rgbStripeDevice; }

WS2812FXDevice *EffectsController::ws2812FXDevice(int port) {
  return ws2812FXDevices[--port][0];
}

CombinedGiAndLightMatrixWS2812FXDevice *EffectsController::giAndLightMatrix(
    int port) {
  return (CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevice(port);
}

CombinedGiAndLightMatrixWS2812FXDevice *
EffectsController::createCombinedGiAndLightMatrixWs2812FXDevice(int port) {
  WS2812FXDevice *ws2812FXDevice = ws2812FXDevices[--port][0];

  CombinedGiAndLightMatrixWS2812FXDevice *giAndLightMatrix =
      new CombinedGiAndLightMatrixWS2812FXDevice(
          ws2812FXDevice->getWS2812FX(), ws2812FXDevice->getFirstLED(),
          ws2812FXDevice->getlastLED(), ws2812FXDevice->getFirstSegment(),
          ws2812FXDevice->getLastSegment(), _eventDispatcher);

  giAndLightMatrix->off();

  ws2812FXDevices[port][0] = giAndLightMatrix;
  delete ws2812FXDevice;

  return giAndLightMatrix;
}

WS2812FXDevice *EffectsController::createWS2812FXDevice(int port, int number,
                                                        int segments,
                                                        int firstLED,
                                                        int lastLED) {
  --port;

  if (number == 0) {
    ws2812FXDevices[port][0]->_reduceLEDs(lastLED, segments - 1);
  } else {
    int firstSegment = ws2812FXDevices[port][number - 1]->getLastSegment() + 1;

    ws2812FXDevices[port][number] =
        new WS2812FXDevice(ws2812FXDevices[port][0]->getWS2812FX(), firstLED,
                           lastLED, firstSegment, firstSegment + segments - 1);

    ++ws2812FXDeviceCounters[port];
  }

  return ws2812FXDevices[port][number];
}

WS2812FXDevice *EffectsController::ws2812FXDevice(int port, int number) {
  return ws2812FXDevices[--port][number];
}

void EffectsController::addEffect(Effect *effect, EffectDevice *device,
                                  Event *event, int priority, int repeat,
                                  int mode) {
  addEffect(new EffectContainer(effect, device, event, priority, repeat, mode));
}

void EffectsController::addEffect(EffectContainer *container) {
  container->effect->setEventDispatcher(this->eventDispatcher());
  container->effect->setDevice(container->device);
  stackEffectContainers[++stackCounter] = container;
}

void EffectsController::attachBrightnessControl(byte port, byte poti) {
  brightnessControl[--port] = poti;
}

void EffectsController::setBrightness(byte port, byte brightness) {
  ws2812FXDevices[--port][0]->setBrightness(brightness);
  ws2812FXbrightness[port] = brightness;
}

void EffectsController::handleEvent(Event *event) {
  switch (event->sourceId) {
    case EVENT_RESET:
      if (_shakerPWMDevice) _shakerPWMDevice->reset();
      if (_ledPWMDevice) _ledPWMDevice->reset();

      for (int i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++) {
        if (ws2812FXstates[i]) {
          for (int k = ws2812FXDeviceCounters[i] - 1; k >= 0; k--) {
            if (ws2812FXDevices[i][k]) {
              delete ws2812FXDevices[i][k];
            }
          }
        }
      }

      break;

    default:
      for (int i = 0; i <= stackCounter; i++) {
        if (event->sourceId == stackEffectContainers[i]->event->sourceId &&
            event->eventId == stackEffectContainers[i]->event->eventId &&
            event->value == stackEffectContainers[i]->event->value &&
            (mode == stackEffectContainers[i]->mode ||
             -1 == stackEffectContainers[i]->mode  // -1 means any mode
             )) {
          for (int k = 0; k <= stackCounter; k++) {
            if (stackEffectContainers[i]->device ==
                    stackEffectContainers[k]->device &&
                stackEffectContainers[k]->effect->isRunning()) {
              if (stackEffectContainers[i]->priority >
                  stackEffectContainers[k]->priority) {
                stackEffectContainers[k]->effect->terminate();
                stackEffectContainers[i]->effect->start(
                    stackEffectContainers[i]->repeat);
              }
              break;
            }
            if (k == stackCounter) {
              stackEffectContainers[i]->effect->start(
                  stackEffectContainers[i]->repeat);
            }
          }
        }
      }
  }
}

void EffectsController::handleEvent(ConfigEvent *event) {
  if (event->boardId == boardId) {
    switch (event->topic) {
      case CONFIG_TOPIC_PLATFORM:
        platform = event->value;
        break;

      case CONFIG_TOPIC_LED_STRING:
        switch (event->key) {
          case CONFIG_TOPIC_PORT:
            config_values[0] = event->value;  // port
            config_neoPixelType = 0;
            config_values[1] = 0;   // amount of leds
            config_values[2] = 0;   // after glow
            config_values[3] = 50;  // brightness
            config_values[4] = 0;   // heat up
            break;
          case CONFIG_TOPIC_TYPE:
            config_neoPixelType = (neoPixelType)event->value;
            break;
          case CONFIG_TOPIC_BRIGHTNESS:
            config_values[3] = event->value;
            break;
          case CONFIG_TOPIC_AMOUNT_LEDS:
            config_values[1] = event->value;
            break;
          case CONFIG_TOPIC_AFTER_GLOW:
            config_values[2] = event->value;
            break;
          case CONFIG_TOPIC_LIGHT_UP:
            config_values[4] = event->value;
            if (!ws2812FXDevices[0][0]) {
              ws2812FXDevices[0][0] =
                  new CombinedGiAndLightMatrixWS2812FXDevice(
                      new WS2812FX(config_values[1], config_values[0],
                                   config_neoPixelType),
                      0, config_values[1] - 1, 0, 0, _eventDispatcher);
              ws2812FXDevices[0][0]->getWS2812FX()->init();
              ws2812FXDeviceCounters[0] = 1;

              // Brightness might be overwritten later.
              ws2812FXDevices[0][0]->setBrightness(config_values[3]);
              // "off" means no effects, standard operation mode.
              ws2812FXDevices[0][0]->off();
              if (config_values[4] > 0) {
                ((CombinedGiAndLightMatrixWS2812FXDevice *)
                     ws2812FXDevices[0][0])
                    ->setHeatUp(config_values[4]);
              }
              if (config_values[2] > 0) {
                ((CombinedGiAndLightMatrixWS2812FXDevice *)
                     ws2812FXDevices[0][0])
                    ->setAfterGlow(config_values[2]);
              }
              ws2812FXstates[0] = true;
            }
            break;
        }
        break;

      case CONFIG_TOPIC_LED_SEGMENT:
        if (ws2812FXDevices[0][0]) {
          switch (event->key) {
            case CONFIG_TOPIC_PORT:
              config_values[0] = event->value;  // port
              config_values[1] = 0;             // number
              config_values[2] = 0;             // from
              config_values[3] = 0;             // to
              break;
            case CONFIG_TOPIC_NUMBER:
              config_values[1] = event->value;
              break;
            case CONFIG_TOPIC_FROM:
              config_values[2] = event->value;
              break;
            case CONFIG_TOPIC_TO:
              config_values[3] = event->value;

              ws2812FXDevices[0][0]->getWS2812FX()->setSegment(
                  config_values[1], config_values[2], config_values[3]);
              break;
          }
        }
        break;

      case CONFIG_TOPIC_LED_EFFECT:
        if (ws2812FXDevices[0][0]) {
          switch (event->key) {
            case CONFIG_TOPIC_PORT:
              config_values[0] = event->value;  // port
              config_values[1] = 0;             // segment
              config_values[2] = 0;             // duration
              config_values[3] = 0;             // effect
              config_values[4] = 0;             // reverse
              config_values[5] = 0;             // speed
              config_values[6] = 0;             // mode
              config_values[7] = 0;             // priority
              config_values[8] = 0;             // repeat
              config_payload = 0;               // color
              ws1812Effect = nullptr;
              break;
            case CONFIG_TOPIC_LED_SEGMENT:
              config_values[1] = event->value;
              break;
            case CONFIG_TOPIC_COLOR:
              config_payload = event->value;
              break;
            case CONFIG_TOPIC_DURATION:
              config_values[2] = event->value;
              break;
            case CONFIG_TOPIC_EFFECT:
              config_values[3] = event->value;
              break;
            case CONFIG_TOPIC_REVERSE:
              config_values[4] = event->value;
              break;
            case CONFIG_TOPIC_SPEED:
              config_values[5] = event->value;
              break;
            case CONFIG_TOPIC_MODE:
              config_values[6] = event->value;
              break;
            case CONFIG_TOPIC_PRIORITY:
              config_values[7] = event->value;
              break;
            case CONFIG_TOPIC_REPEAT:
              config_values[8] = event->value;
              ws1812Effect = new WS2812FXEffect(
                  config_values[1], config_values[3], config_payload,
                  config_values[5],
                  config_values[4] == 1 ? REVERSE : NO_OPTIONS,
                  config_values[2]);
              break;
          }
        }
        break;

      case CONFIG_TOPIC_TRIGGER:
        switch (event->key) {
          case CONFIG_TOPIC_PORT:
            config_values[0] = event->value;  // port
            config_values[1] = 0;             // type
            config_values[2] = 0;             // source
            config_values[3] = 0;             // number
            config_values[4] = 0;             // value
            break;
          case CONFIG_TOPIC_TYPE:
            config_values[1] = event->value;
            break;
          case CONFIG_TOPIC_SOURCE:
            config_values[2] = event->value;  // source
            break;
          case CONFIG_TOPIC_NUMBER:
            config_values[3] = event->value;
            break;
          case CONFIG_TOPIC_VALUE:
            config_values[4] = event->value;
            switch (config_values[1]) {
              case CONFIG_TOPIC_LED_EFFECT:
                if (ws1812Effect) {
                  addEffect(
                      ws1812Effect, ws2812FXDevices[0][0],
                      new Event(config_values[2], config_values[3],
                                config_values[4]),
                      config_values[7],  // priority
                      config_values[8] == 255 ? -1
                                              : config_values[8],  // repeat
                      config_values[6]                             // mode
                  );
                }
                break;

              case CONFIG_TOPIC_PWM_EFFECT:
                if (pwmEffect) {
                  addEffect(
                      pwmEffect, _shakerPWMDevice,
                      new Event(config_values[2], config_values[3],
                                config_values[4]),
                      config_values[7],  // priority
                      config_values[8] == 255 ? -1
                                              : config_values[8],  // repeat
                      config_values[6]                             // mode
                  );
                }
                break;
            }
            break;
        }
        break;

      case CONFIG_TOPIC_LAMPS:
        if (ws2812FXDevices[0][0]) {
          switch (event->key) {
            case CONFIG_TOPIC_PORT:
              config_values[0] = event->value;  // port
              config_values[1] = 0;             // type
              config_values[2] = 0;             // number
              config_values[3] = 0;             // led number
              config_payload = 0;               // color
              break;
            case CONFIG_TOPIC_TYPE:
              config_values[1] = event->value;
              break;
            case CONFIG_TOPIC_NUMBER:
              config_values[2] = event->value;
              break;
            case CONFIG_TOPIC_LED_NUMBER:
              config_values[3] = event->value;
              break;
            case CONFIG_TOPIC_COLOR:
              config_payload = event->value;
              switch (config_values[1]) {
                case LED_TYPE_GI:
                  ((CombinedGiAndLightMatrixWS2812FXDevice *)
                       ws2812FXDevices[0][0])
                      ->assignLedToGiString(config_values[2], config_values[3],
                                            config_payload);
                  break;
                case LED_TYPE_LAMP:
                  if (config_values[2] >= CUSTOM_LED_OFFSET) {
                    ((CombinedGiAndLightMatrixWS2812FXDevice *)
                         ws2812FXDevices[0][0])
                        ->assignCustomLed(config_values[2], config_values[3],
                                          config_payload);
                  } else if (platform == PLATFORM_WPC) {
                    ((CombinedGiAndLightMatrixWS2812FXDevice *)
                         ws2812FXDevices[0][0])
                        ->assignLedToLightMatrixWPC(
                            config_values[2], config_values[3], config_payload);
                  } else {
                    ((CombinedGiAndLightMatrixWS2812FXDevice *)
                         ws2812FXDevices[0][0])
                        ->assignLedToLightMatrixDE(
                            config_values[2], config_values[3], config_payload);
                  }
                  break;
                case LED_TYPE_FLASHER:
                  ((CombinedGiAndLightMatrixWS2812FXDevice *)
                       ws2812FXDevices[0][0])
                      ->assignLedToFlasher(config_values[2], config_values[3],
                                           config_payload);
                  break;
              }
              break;
          }
        }
        break;

      case CONFIG_TOPIC_PWM:
        switch (event->key) {
          case CONFIG_TOPIC_PORT:
            config_values[0] = event->value;  // port
            config_values[1] = 0;             // power
          case CONFIG_TOPIC_POWER:
            config_values[1] = event->value;
            break;
          case CONFIG_TOPIC_TYPE:
            switch (event->value) {
              case PWM_TYPE_SHAKER:  // Shaker
                _shakerPWMDevice = new WavePWMDevice(
                    config_values[0], config_values[1], _eventDispatcher);
                _shakerPWMDevice->off();
                break;
            }
            break;
        }
        break;

      case CONFIG_TOPIC_PWM_EFFECT:
        if (_shakerPWMDevice) {
          switch (event->key) {
            case CONFIG_TOPIC_PORT:
              config_values[0] = event->value;  // port
              config_values[1] = 0;             // duration
              config_values[2] = 0;             // effect
              config_values[3] = 0;             // frequency
              config_values[4] = 0;             // max intensity
              config_values[5] = 0;             // min intensity
              config_values[6] = 0;             // mode
              config_values[7] = 0;             // priority
              config_values[8] = 0;             // repeat
              config_payload = 0;               // color
              pwmEffect = nullptr;
              break;
            case CONFIG_TOPIC_DURATION:
              config_values[1] = event->value;
              break;
            case CONFIG_TOPIC_EFFECT:
              config_values[2] = event->value;
              break;
            case CONFIG_TOPIC_FREQUENCY:
              config_values[3] = event->value;
              break;
            case CONFIG_TOPIC_MAX_INTENSITY:
              config_values[4] = event->value;
              break;
            case CONFIG_TOPIC_MIN_INTENSITY:
              config_values[5] = event->value;
              break;
            case CONFIG_TOPIC_MODE:
              config_values[6] = event->value;
              break;
            case CONFIG_TOPIC_PRIORITY:
              config_values[7] = event->value;
              break;
            case CONFIG_TOPIC_REPEAT:
              config_values[8] = event->value;
              switch (config_values[2]) {
                case PWM_EFFECT_SINE:
                  pwmEffect =
                      new SinePWMEffect(config_values[3], config_values[1],
                                        config_values[4], config_values[5]);
                  break;
                case PWM_EFFECT_IMPULSE:
                  pwmEffect =
                      new ImpulsePWMEffect(config_values[3], config_values[4]);
                  break;
                case PWM_EFFECT_RAMP_DOWN_STOP:
                  pwmEffect = new RampDownStopPWMEffect(config_values[3]);
                  break;
              }
              break;
          }
        }
        break;
    }
  }
}

void EffectsController::update() {
  _eventDispatcher->update();

  for (int i = 0; i <= stackCounter; i++) {
    if (stackEffectContainers[i]->effect->isRunning()) {
      stackEffectContainers[i]->effect->updateMillis();
      stackEffectContainers[i]->effect->update();
    }
  }

  if (millis() - ws2812UpdateInterval > UPDATE_INTERVAL_WS2812FX_EFFECTS) {
    // Updating the LEDs too fast leads to undefined behavior. Just update
    // effects every 3ms.
    ws2812UpdateInterval = millis();

    for (int i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++) {
      if (ws2812FXstates[i]) {
        if (ws2812FXrunning[i]) {
          ws2812FXDevices[i][0]->getWS2812FX()->service();

          bool stop = true;
          for (int k = 0; k < ws2812FXDeviceCounters[i]; k++) {
            stop &= ws2812FXDevices[i][k]->isStopped();
          }

          if (stop) {
            ws2812FXDevices[i][0]->getWS2812FX()->stop();
            ws2812FXrunning[i] = false;
          }
        } else {
          bool start = false;
          for (int k = 0; k < ws2812FXDeviceCounters[i]; k++) {
            start |= !ws2812FXDevices[i][k]->isStopped();
          }

          if (start) {
            ws2812FXDevices[i][0]->getWS2812FX()->start();
            ws2812FXrunning[i] = true;
            ws2812FXDevices[i][0]->getWS2812FX()->service();
          }
        }
      }
    }
  }

  if (millis() - ws2812AfterGlowUpdateInterval >
      UPDATE_INTERVAL_WS2812FX_AFTERGLOW) {
    // Updating the LEDs too fast leads to undefined behavior. Just update
    // every 3ms.
    ws2812AfterGlowUpdateInterval = millis();
    for (int i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++) {
      if (ws2812FXstates[i] && ws2812FXDevices[i][0]->hasAfterGlowSupport() &&
          !ws2812FXrunning[i]) {
        // No other effect is running, handle after glow effect.
        ((CombinedGiAndLightMatrixWS2812FXDevice *)ws2812FXDevices[i][0])
            ->updateAfterGlow();
        ws2812FXDevices[i][0]->getWS2812FX()->show();
      }
    }
  }

  if (brightnessControlBasePin > 0) {
    if (millis() - brightnessUpdateInterval >
        UPDATE_INTERVAL_WS2812FX_BRIGHTNESS) {
      // Don't update the brightness too often.
      brightnessUpdateInterval = millis();
      for (byte i = 0; i < PPUC_MAX_BRIGHTNESS_CONTROLS; i++) {
        brightnessControlReads[i] =
            analogRead(brightnessControlBasePin + i) / 4;
      }
      for (byte i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++) {
        if (brightnessControl[i] > 0) {
          setBrightness(i + 1,
                        brightnessControlReads[brightnessControl[i - 1]]);
        }
      }
    }
  }
}

void EffectsController::start() {
  for (int i = 0; i < PPUC_MAX_WS2812FX_DEVICES; i++) {
    if (ws2812FXbrightness[i] == 0) {
      // setBrightness(i + 1, WS2812FX_BRIGHTNESS);
    }
  }

  _eventDispatcher->dispatch(new Event(EVENT_SOURCE_EFFECT, 1, 255));
}
