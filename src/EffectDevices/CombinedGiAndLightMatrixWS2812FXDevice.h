/*
  CombinedGiAndLightMatrixWS2812FXDevice.h
  Created by Markus Kalkbrenner, 2021 - 2025.

  WPC matrix numbering:

     | C1 | C2 | C3 | C4 | C5 | C6 | C7 | C8
  ---+----+----+----+----+----+----+----+----
  R1 | 11 | 21 | 31 | 41 | 51 | 61 | 71 | 81
  ---+----+----+----+----+----+----+----+----
  R2 | 12 | 22 | 32 | 42 | 52 | 62 | 72 | 82
  ---+----+----+----+----+----+----+----+----
  R3 | 13 | 23 | 33 | 43 | 53 | 63 | 73 | 83
  ---+----+----+----+----+----+----+----+----
  R4 | 14 | 24 | 34 | 44 | 54 | 64 | 74 | 84
  ---+----+----+----+----+----+----+----+----
  R5 | 15 | 25 | 35 | 45 | 55 | 65 | 75 | 85
  ---+----+----+----+----+----+----+----+----
  R6 | 16 | 26 | 36 | 46 | 56 | 66 | 76 | 86
  ---+----+----+----+----+----+----+----+----
  R7 | 17 | 27 | 37 | 47 | 57 | 67 | 77 | 87
  ---+----+----+----+----+----+----+----+----
  R8 | 18 | 28 | 38 | 48 | 58 | 68 | 78 | 88

  DE and SYS4-SYS11 matrix numbering:

     | C1 | C2 | C3 | C4 | C5 | C6 | C7 | C8
  ---+----+----+----+----+----+----+----+----
  R1 |  1 |  9 | 17 | 25 | 33 | 41 | 49 | 57
  ---+----+----+----+----+----+----+----+----
  R2 |  2 | 10 | 18 | 26 | 34 | 42 | 50 | 58
  ---+----+----+----+----+----+----+----+----
  R3 |  3 | 11 | 19 | 27 | 35 | 43 | 51 | 59
  ---+----+----+----+----+----+----+----+----
  R4 |  4 | 12 | 20 | 28 | 36 | 44 | 52 | 60
  ---+----+----+----+----+----+----+----+----
  R5 |  5 | 13 | 21 | 29 | 37 | 45 | 53 | 61
  ---+----+----+----+----+----+----+----+----
  R6 |  6 | 14 | 22 | 30 | 38 | 46 | 54 | 62
  ---+----+----+----+----+----+----+----+----
  R7 |  7 | 15 | 23 | 31 | 39 | 47 | 55 | 63
  ---+----+----+----+----+----+----+----+----
  R8 |  8 | 16 | 24 | 32 | 40 | 48 | 56 | 64

  Whitestar and early SAM matrix numbering:

     | C1 | C2 | C3 | C4 | C5 | C6 | C7 | C8
  ---+----+----+----+----+----+----+----+----
  R1 |  1 |  2 |  3 |  4 |  5 |  6 |  7 |  8
  ---+----+----+----+----+----+----+----+----
  R2 |  9 | 10 | 11 | 12 | 13 | 14 | 15 | 16
  ---+----+----+----+----+----+----+----+----
  R3 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24
  ---+----+----+----+----+----+----+----+----
  R4 | 25 | 26 | 27 | 28 | 29 | 30 | 31 | 32
  ---+----+----+----+----+----+----+----+----
  R5 | 33 | 34 | 35 | 36 | 37 | 38 | 39 | 40
  ---+----+----+----+----+----+----+----+----
  R6 | 41 | 42 | 43 | 44 | 45 | 46 | 47 | 48
  ---+----+----+----+----+----+----+----+----
  R7 | 49 | 50 | 51 | 52 | 53 | 54 | 55 | 56
  ---+----+----+----+----+----+----+----+----
  R8 | 57 | 58 | 59 | 60 | 61 | 62 | 63 | 64
  ---+----+----+----+----+----+----+----+----
  R9 | 65 | 66 | 67 | 68 | 69 | 70 | 71 | 72
  ---+----+----+----+----+----+----+----+----
  R10| 73 | 74 | 75 | 76 | 77 | 78 | 79 | 80

  Bally 35 is speecial. It can drive 60 lamps. But using relays, 60 different
  lamps could be driven, for example used in Elektra to drive the lamps of two
  different playfields. In libpiname that is handled using a standard 8x8 matrix
  and adding a second (virtual) 8x8 matrix. We don't see the state of the relay,
  but get dedicated lamp numbers from 1-60 and 65-124. So lamps that get
  triggered in combination with the relay get offset of 64 to their number. So
  we have 120 lamps out of 128.

  Capcom uses two real 8x8 matrix and has no GI. So we have 128 CPU-controlled
  lamps.

  In order to ease the AfterGlow handling and to avoid long iterations across
  arrays and to reduce the number of addressable LED strings, we extend the
  original Lamp Matrix.
  Starting at position 129, we add custom LEDs which are added to the playfield
  and which are not part of the original matrix.
*/

#ifndef CombinedGiAndLightMatrixWS2812FXDevice_h
#define CombinedGiAndLightMatrixWS2812FXDevice_h

#include <Arduino.h>
#include <WavePWM.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "../EventDispatcher/Event.h"
#include "../EventDispatcher/EventDispatcher.h"
#include "../EventDispatcher/EventListener.h"
#include "../PPUC.h"
#include "WS2812FXDevice.h"

// Number of WPC GI strings
#define NUM_GI_STRINGS 5

struct LED {
  uint8_t number;      // Original system number (GI string, light matrix pos,
                       // flasher port)
  uint16_t position;   // Position in WS2812 stripe
  uint32_t color;      // LED color
  uint32_t heatUp;     // Heat up effect value
  uint32_t afterGlow;  // After glow effect value

  LED(uint8_t num, uint16_t pos, uint32_t col = 0)
      : number(num), position(pos), color(col), heatUp(0), afterGlow(0) {}
};

class CombinedGiAndLightMatrixWS2812FXDevice : public WS2812FXDevice,
                                               public EventListener {
 public:
  CombinedGiAndLightMatrixWS2812FXDevice(WS2812FX* ws2812FX, int firstLED,
                                         int lastLED, int firstSegment,
                                         int lastSegment,
                                         EventDispatcher* eventDispatcher)
      : WS2812FXDevice(ws2812FX, firstLED, lastLED, firstSegment, lastSegment) {
    wavePWMHeatUp = new WavePWM();
    wavePWMAfterGlow = new WavePWM();
    afterGlowSupport = true;

    eventDispatcher->addListener(this, EVENT_SOURCE_GI);
    eventDispatcher->addListener(this, EVENT_SOURCE_SOLENOID);
    eventDispatcher->addListener(this, EVENT_SOURCE_LIGHT);
  }

  ~CombinedGiAndLightMatrixWS2812FXDevice() {
    delete wavePWMHeatUp;
    delete wavePWMAfterGlow;
  }

  void on();
  void off();

  void assignLedToGiString(uint8_t number, int16_t led, uint32_t color = 0) {
    giLEDs.emplace_back(number, led, color);
    rebuildGIIndex();
  }

  void assignLedToLightMatrix(uint8_t number, int16_t led, uint32_t color = 0) {
    lightMatrixLEDs.emplace_back(number, led, color);
    rebuildLightMatrixIndex();
  }

  void assignLedToFlasher(uint8_t number, int16_t led, uint32_t color = 0) {
    flasherLEDs.emplace_back(number, led, color);
    rebuildFlasherIndex();
  }

  // Fast access methods
  std::vector<LED*> getGILEDsByNumber(uint8_t number) {
    auto it = giIndex.find(number);
    if (it != giIndex.end()) return it->second;
    return {};
  }

  std::vector<LED*> getLightMatrixLEDsByNumber(uint8_t number) {
    auto it = lightMatrixIndex.find(number);
    if (it != lightMatrixIndex.end()) return it->second;
    return {};
  }

  std::vector<LED*> getFlasherLEDsByNumber(uint8_t number) {
    auto it = flasherIndex.find(number);
    if (it != flasherIndex.end()) return it->second;
    return {};
  }

  std::vector<LED*> getChangingGILEDsByNumber(uint8_t number) {
    std::vector<LED*> result;
    for (auto led : getGILEDsByNumber(number)) {
      if (led->heatUp > 0 || led->afterGlow > 0) {
        result.push_back(led);
      }
    }
    return result;
  }

  std::vector<LED*> getChangingLightMatrixAndFlasherLEDs() {
    std::vector<LED*> result;
    for (auto& led : getLightMatrixLEDs()) {
      if (led.heatUp > 0 || led.afterGlow > 0) {
        result.push_back(&led);  // Use &led to get the pointer
      }
    }
    for (auto& led : getFlasherLEDs()) {
      if (led.heatUp > 0 || led.afterGlow > 0) {
        result.push_back(&led);  // Use &led to get the pointer
      }
    }
    return result;
  }

  // Direct access to vectors for iteration
  std::vector<LED>& getGILEDs() { return giLEDs; }
  std::vector<LED>& getLightMatrixLEDs() { return lightMatrixLEDs; }
  std::vector<LED>& getFlasherLEDs() { return flasherLEDs; }

  uint32_t getDimmedPixelColor(uint32_t color, uint8_t brightness);
  void setHeatUp();
  void setAfterGlow();
  void setHeatUp(int ms);
  void setAfterGlow(int ms);
  void handleEvent(Event* event);
  void handleEvent(ConfigEvent* event) {}
  void updateAfterGlow();

 protected:
  std::vector<LED> giLEDs;
  std::vector<LED> lightMatrixLEDs;
  std::vector<LED> flasherLEDs;

  // Indexes for fast lookup by number
  std::unordered_map<uint8_t, std::vector<LED*>> giIndex;
  std::unordered_map<uint8_t, std::vector<LED*>> lightMatrixIndex;
  std::unordered_map<uint8_t, std::vector<LED*>> flasherIndex;

  uint8_t sourceGIBrightness[NUM_GI_STRINGS] = {0};
  uint8_t targetGIBrightness[NUM_GI_STRINGS] = {0};

  void intializeNewLEDState(LED* led, bool on);

  void rebuildGIIndex() {
    giIndex.clear();
    for (auto& led : giLEDs) {
      giIndex[led.number].push_back(&led);
    }
  }

  void rebuildLightMatrixIndex() {
    lightMatrixIndex.clear();
    for (auto& led : lightMatrixLEDs) {
      lightMatrixIndex[led.number].push_back(&led);
    }
  }

  void rebuildFlasherIndex() {
    flasherIndex.clear();
    for (auto& led : flasherLEDs) {
      flasherIndex[led.number].push_back(&led);
    }
  }

  WavePWM* wavePWMHeatUp;
  WavePWM* wavePWMAfterGlow;

  // When no effects are running, we're in normal GI and Light Matrix mode.
  bool stopped = false;  // Never stop the updates.
  bool effectRunning = false;
  int16_t msHeatUp = 0;
  int16_t msAfterGlow = 0;
};

#endif
