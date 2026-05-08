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
#include "Effects/FastLedBlinkEffect.h"
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
#define PPUC_MAX_EFFECT_PORTS 32

class EffectsController : public EventListener {
 public:
  EffectsController(int ct, int pf) : EventListener() {
    controllerType = ct;
    platform = pf;

    effectsControllerInstance = this;
    _eventDispatcher = new EventDispatcher();
    _eventDispatcher->addListener(this);
  }

  void begin() {
    if (initialized) {
      return;
    }

    if (controllerType == CONTROLLER_16_8_1) {
      boardId = static_cast<byte>(readBoardId());

      _ledBuiltInDevice = new LedBuiltInDevice();
      _ledBuiltInDevice->on();

      addEffect(new FastLedBlinkEffect(), _ledBuiltInDevice,
                new Event(EVENT_ERROR), 3, -1, -1);
      addEffect(new LedOnEffect(), _ledBuiltInDevice, new Event(EVENT_NO_ERROR),
                4, 0, -1);
      addEffect(new LedBlinkEffect(), _ledBuiltInDevice, new Event(EVENT_RUN),
                1,   // priority
                -1,  // repeat
                0    // mode
      );
      builtInEffectCount = stackCounter + 1;
      initialized = true;
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
  int findEffectContainer(const EffectContainer* candidate) const;
  int findRunningEffectOnDevice(const EffectDevice* device) const;
  int findBestSuspendedEffectForDevice(const EffectDevice* device) const;
  void resumeSuspendedEffects();
  void clearConfiguredEffects();

  int readBoardId() const {
    delay(2);
    uint8_t votes[8] = {0};
    for (uint8_t i = 0; i < 8; ++i) {
      const int raw = analogRead(28);
      const uint8_t decoded =
          static_cast<uint8_t>((16 - static_cast<int>((raw + 29.23) / 58.46)) &
                               0b0111);
      votes[decoded]++;
      delay(2);
    }

    uint8_t bestValue = 0;
    uint8_t bestVotes = 0;
    for (uint8_t value = 0; value < 8; ++value) {
      if (votes[value] > bestVotes) {
        bestVotes = votes[value];
        bestValue = value;
      }
    }

    return bestValue;
  }

  EventDispatcher* _eventDispatcher;
  LedBuiltInDevice* _ledBuiltInDevice = nullptr;
  NullDevice* _nullDevice = nullptr;
  WavePWMDevice* _shakerPWMDevice = nullptr;
  WavePWMDevice* _ledPWMDevice = nullptr;
  RgbStripDevice* _rgbStripeDevice = nullptr;
  WS2812FXDevice* ws2812FXDevices[PPUC_MAX_WS2812FX_DEVICES][10];
  int ws2812FXDeviceCounters[PPUC_MAX_WS2812FX_DEVICES] = {0};
  bool ws2812FXstates[PPUC_MAX_WS2812FX_DEVICES] = {0};
  bool ws2812FXrunning[PPUC_MAX_WS2812FX_DEVICES] = {0};
  byte ws2812FXbrightness[PPUC_MAX_WS2812FX_DEVICES] = {0};
  WS2812FXDevice* ws2812FXDeviceByPort[PPUC_MAX_EFFECT_PORTS] = {nullptr};
  WavePWMDevice* pwmEffectDeviceByPort[PPUC_MAX_EFFECT_PORTS] = {nullptr};
  EffectContainer* stackEffectContainers[EFFECT_STACK_SIZE];
  int stackCounter = -1;
  int builtInEffectCount = 0;
  bool flickerState = false;
  int mode = 0;

  byte platform;
  byte controllerType;
  byte boardId = 255;
  bool initialized = false;
  byte config_port = 0;
  uint32_t config_values[9] = {0};
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
