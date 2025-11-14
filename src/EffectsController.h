/*
  EffectsController.h
  Created by Markus Kalkbrenner, 2021.

  Play more pinball!
*/

#ifndef EFFECTSCONTROLLER_h
#define EFFECTSCONTROLLER_h

#include <Adafruit_NeoPixel.h>
#include <WS2812FX.h>

#include "EffectDevices/CombinedGiAndLightMatrixWS2812FXDevice.h"
#include "EffectDevices/LedBuiltInDevice.h"
#include "EffectDevices/NullDevice.h"
#include "EffectDevices/RgbStripDevice.h"
#include "EffectDevices/WS2812FXDevice.h"
#include "EffectDevices/WavePWMDevice.h"
#include "Effects/Effect.h"
#include "Effects/EffectContainer.h"
#include "Effects/ImpulsePWMEffect.h"
#include "Effects/LedBlinkEffect.h"
#include "Effects/LedOnEffect.h"
#include "Effects/NullEffect.h"
#include "Effects/RGBColorCycleEffect.h"
#include "Effects/RampDownStopPWMEffect.h"
#include "Effects/SinePWMEffect.h"
#include "Effects/WS2812FXEffect.h"
#include "EventDispatcher/CrossLinkDebugger.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "EventDispatcher/EventListener.h"
#include "PPUC.h"

#ifndef EFFECT_STACK_SIZE
#define EFFECT_STACK_SIZE 128
#endif

#define WS2812FX_BRIGHTNESS 64

#define UPDATE_INTERVAL_WS2812FX_EFFECTS 3
#define UPDATE_INTERVAL_WS2812FX_AFTERGLOW 3
#define UPDATE_INTERVAL_WS2812FX_BRIGHTNESS 10

#define PPUC_MAX_WS2812FX_DEVICES 1
#define PPUC_MAX_BRIGHTNESS_CONTROLS 1

class EffectsController : public EventListener {
 public:
  EffectsController(int ct, int pf) : EventListener() {
    controllerType = ct;
    platform = pf;

    effectsControllerInstance = this;
    _eventDispatcher = new EventDispatcher();
    _eventDispatcher->addListener(this);

    if (controllerType == CONTROLLER_16_8_1) {
      // Read bordID. Ideal value at 10bit resolution: (DIP+1)*1023*2/35
      // -> 58.46 to 935.3
      boardId = 16 - ((int)((analogRead(28) + 29.23) / 58.46));

      _ledBuiltInDevice = new LedBuiltInDevice();
      _ledBuiltInDevice->on();

      addEffect(new LedBlinkEffect(), _ledBuiltInDevice, new Event(EVENT_RUN),
                1,   // priority
                -1,  // repeat
                0    // mode
      );
    } else {
      Serial.print("Unsupported Effects Controller: ");
      Serial.println(controllerType);
    }
  }

  EventDispatcher* eventDispatcher();

  LedBuiltInDevice* ledBuiltInDevice();

  NullDevice* nullDevice();

  WavePWMDevice* shakerPWMDevice();

  WavePWMDevice* ledPWMDevice();

  RgbStripDevice* rgbStripDevice();

  WS2812FXDevice* ws2812FXDevice(int port);

  CombinedGiAndLightMatrixWS2812FXDevice*
  createCombinedGiAndLightMatrixWs2812FXDevice(int port);

  CombinedGiAndLightMatrixWS2812FXDevice* giAndLightMatrix(int port);

  WS2812FXDevice* createWS2812FXDevice(int port, int number, int segments,
                                       int firstLED, int lastLED);

  WS2812FXDevice* ws2812FXDevice(int port, int number);

  void addEffect(Effect* effect, EffectDevice* device, Event* event,
                 int priority, int repeat, int mode);

  // void addEffect(Effect* effect, EffectDevice* device, EventSequence*
  // sequence, int priority, int repeat);

  void addEffect(EffectContainer* container);

  void attachBrightnessControl(byte port, byte poti);

  void setBrightness(byte port, byte brightness);

  void start();

  void update();

  void handleEvent(Event* event);

  void handleEvent(ConfigEvent* event);

 private:
  EventDispatcher* _eventDispatcher;
  LedBuiltInDevice* _ledBuiltInDevice;
  NullDevice* _nullDevice;
  WavePWMDevice* _shakerPWMDevice;
  WavePWMDevice* _ledPWMDevice;
  RgbStripDevice* _rgbStripeDevice;
  WS2812FXDevice* ws2812FXDevices[PPUC_MAX_WS2812FX_DEVICES][10];
  int ws2812FXDeviceCounters[PPUC_MAX_WS2812FX_DEVICES] = {0};
  bool ws2812FXstates[PPUC_MAX_WS2812FX_DEVICES] = {0};
  bool ws2812FXrunning[PPUC_MAX_WS2812FX_DEVICES] = {0};
  byte ws2812FXbrightness[PPUC_MAX_WS2812FX_DEVICES] = {0};
  EffectContainer* stackEffectContainers[EFFECT_STACK_SIZE];
  int stackCounter = -1;
  byte brightnessControl[PPUC_MAX_WS2812FX_DEVICES] = {0};
  byte brightnessControlReads[PPUC_MAX_BRIGHTNESS_CONTROLS] = {0};
  byte brightnessControlBasePin = 0;

  int mode = 0;

  byte platform;
  byte controllerType;
  byte boardId = 255;
  byte config_port = 0;
  byte config_values[9] = {0};
  neoPixelType config_neoPixelType = 0;
  uint32_t config_payload = 0;
  WS2812FXEffect* ws1812Effect;
  WavePWMEffect* pwmEffect;

  unsigned long ws2812UpdateInterval = 0;
  unsigned long ws2812AfterGlowUpdateInterval = 0;
  unsigned long brightnessUpdateInterval = 0;

  static EffectsController* effectsControllerInstance;
};

#endif
