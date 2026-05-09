#include "CombinedGiAndLightMatrixWS2812FXDevice.h"

void CombinedGiAndLightMatrixWS2812FXDevice::on() {
  WS2812FXDevice::on();
  effectRunning = true;
}

void CombinedGiAndLightMatrixWS2812FXDevice::off() {
  effectRunning = false;
  // No stop. Just reset to quit effects and return to standard GI and Light
  // Matrix operation.
  reset();
}

void CombinedGiAndLightMatrixWS2812FXDevice::handleEvent(Event* event) {
  if (!effectRunning) {
    if (event->sourceId == EVENT_SOURCE_GI) {
      const uint8_t giIndex = event->eventId - 1;
      if (giIndex >= NUM_GI_STRINGS) {
        return;
      }
      const uint8_t giStringNumber = event->eventId;
      // Brightness is a value between 0 and 8. Convert it into a value from 0
      // to 255.
      uint8_t giBrightness = 0;
      switch (event->value) {
        case 1:
          giBrightness = 35;
          break;
        case 2:
          giBrightness = 60;
          break;
        case 3:
          giBrightness = 85;
          break;
        case 4:
          giBrightness = 115;
          break;
        case 5:
          giBrightness = 155;
          break;
        case 6:
          giBrightness = 205;
          break;
        case 7:
        case 8:
          giBrightness = 255;
          break;
      }

      if (targetGIBrightness[giIndex] != giBrightness) {
        if ((targetGIBrightness[giIndex] < giBrightness && msHeatUp == 0) ||
            (targetGIBrightness[giIndex] > giBrightness && msAfterGlow == 0)) {
          for (auto& led : getGILEDsByNumber(giStringNumber)) {
            ws2812FX->setPixelColor(
                led->position, getDimmedPixelColor(led->color, giBrightness));
            needsShow = true;
          }
        } else {
          for (auto& led : getGILEDsByNumber(giStringNumber)) {
            // Treat increasing GI brightness like "on" and decreasing
            // brightness like "off" so heat-up/afterglow follow the actual
            // transition direction on mixed GI/lamp strings.
            intializeNewLEDState(led,
                                 targetGIBrightness[giIndex] < giBrightness);
          }
        }

        sourceGIBrightness[giIndex] = targetGIBrightness[giIndex];
        targetGIBrightness[giIndex] = giBrightness;
      }
    } else if (event->sourceId == EVENT_SOURCE_LIGHT) {
      uint8_t number = event->eventId;
      bool on = (bool)event->value;
      for (auto& led : getLightMatrixLEDsByNumber(number)) {
        intializeNewLEDState(led, on);
      }
    } else if (event->sourceId == EVENT_SOURCE_SOLENOID) {
      uint8_t number = event->eventId;
      bool on = (bool)event->value;
      for (auto& led : getFlasherLEDsByNumber(number)) {
        intializeNewLEDState(led, on);
      }
    }
  }
}

void CombinedGiAndLightMatrixWS2812FXDevice::intializeNewLEDState(LED* led,
                                                                  bool on) {
  if (on && msHeatUp == 0) {
    ws2812FX->setPixelColor(led->position, led->color);
    needsShow = true;
  } else if (!on && msAfterGlow == 0) {
    ws2812FX->setPixelColor(led->position, RGBW_BLACK);
    needsShow = true;
  } else if (on) {
    if (led->heatUp == 0 && led->afterGlow == 0) {
      led->heatUp = millis();
    } else if (led->afterGlow > 0) {
      // There's still an after glow effect running. Start heat up
      // from current value.
      uint8_t value = wavePWMAfterGlow->getExponentialValue(
          millis() - led->afterGlow + msAfterGlow);
      led->afterGlow = 0;
      for (int ms = 1; ms <= msHeatUp; ms++) {
        if (wavePWMHeatUp->getExponentialValue(ms) >= value) {
          led->heatUp = millis() - ms;
          break;
        }
      }
      // safety net
      if (led->heatUp == 0) {
        led->heatUp = millis() - msHeatUp + 1;
      }
    }
  } else {
    if (led->afterGlow == 0 && led->heatUp == 0) {
      led->afterGlow = millis();
    } else if (led->heatUp > 0) {
      // There's still a heat up effect running. Start after glow from
      // current value.
      uint8_t value =
          wavePWMHeatUp->getExponentialValue(millis() - led->heatUp);
      led->heatUp = 0;
      for (int ms = msAfterGlow; ms <= (msAfterGlow * 2); ms++) {
        if (wavePWMAfterGlow->getExponentialValue(ms) <= value) {
          led->afterGlow = millis() - ms;
          break;
        }
      }
      // safety net
      if (led->afterGlow == 0) {
        led->afterGlow = millis() - (2 * msAfterGlow) + 1;
      }
    }
  }
}

void CombinedGiAndLightMatrixWS2812FXDevice::updateAfterGlow() {
  for (uint8_t giIndex = 0; giIndex < NUM_GI_STRINGS; giIndex++) {
    uint8_t glowBrightness = targetGIBrightness[giIndex];
    const uint8_t giStringNumber = giIndex + 1;

    for (auto& led : getChangingGILEDsByNumber(giStringNumber)) {
      if (led->heatUp > 0) {
        if ((millis() - led->heatUp) >= msHeatUp) {
          led->heatUp = 0;
        } else {
          float diff =
              targetGIBrightness[giIndex] - sourceGIBrightness[giIndex];
          float mult = diff / 255;
          glowBrightness =
              sourceGIBrightness[giIndex] +
              (wavePWMHeatUp->getExponentialValue(millis() - led->heatUp) *
               mult);
        }
      } else if (led->afterGlow > 0) {
        if ((millis() - led->afterGlow) >= msAfterGlow) {
          led->afterGlow = 0;
        } else {
          float diff =
              sourceGIBrightness[giIndex] - targetGIBrightness[giIndex];
          float mult = diff / 255;
          glowBrightness = targetGIBrightness[giIndex] +
                           (wavePWMAfterGlow->getExponentialValue(
                                millis() - led->afterGlow + msAfterGlow) *
                            mult);
        }
      }

      ws2812FX->setPixelColor(led->position,
                              getDimmedPixelColor(led->color, glowBrightness));
      needsShow = true;
    }
  }

  for (auto& led : getChangingLightMatrixAndFlasherLEDs()) {
    uint8_t glowBrightness;
    if (led->heatUp > 0) {
      if ((millis() - led->heatUp) >= msHeatUp) {
        led->heatUp = 0;
        ws2812FX->setPixelColor(led->position, led->color);
        needsShow = true;
      } else {
        glowBrightness =
            wavePWMHeatUp->getExponentialValue(millis() - led->heatUp);
      }
    } else if (led->afterGlow > 0) {
      if ((millis() - led->afterGlow) >= msAfterGlow) {
        led->afterGlow = 0;
        ws2812FX->setPixelColor(led->position, RGBW_BLACK);
        needsShow = true;
      } else {
        glowBrightness = wavePWMAfterGlow->getExponentialValue(
            millis() - led->afterGlow + msAfterGlow);
      }
    }

    if (led->heatUp > 0 || led->afterGlow > 0) {
      ws2812FX->setPixelColor(led->position,
                              getDimmedPixelColor(led->color, glowBrightness));
      needsShow = true;
    }
  }
}

uint32_t CombinedGiAndLightMatrixWS2812FXDevice::getDimmedPixelColor(
    uint32_t color, uint8_t brightness) {
  uint8_t w = (color >> 24) & 0xFF;
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;

  // uint32_t for more space during the operation
  uint32_t mult = brightness + 1;
  b = (b * mult) >> 8;
  g = (g * mult) >> 8;
  r = (r * mult) >> 8;
  w = (w * mult) >> 8;

  return (uint32_t(w) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) |
         uint32_t(b);
}

void CombinedGiAndLightMatrixWS2812FXDevice::setHeatUp() { setHeatUp(30); }

void CombinedGiAndLightMatrixWS2812FXDevice::setAfterGlow() {
  setAfterGlow(400);
}

void CombinedGiAndLightMatrixWS2812FXDevice::setHeatUp(int ms) {
  wavePWMHeatUp->setup(ms * 2);
  msHeatUp = ms;
}

void CombinedGiAndLightMatrixWS2812FXDevice::setAfterGlow(int ms) {
  wavePWMAfterGlow->setup(ms * 2);
  msAfterGlow = ms;
}
