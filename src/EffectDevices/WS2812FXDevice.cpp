#include "WS2812FXDevice.h"

void WS2812FXDevice::on() {
  reset();
  stopped = false;
}

void WS2812FXDevice::off() {
  reset();
  stopped = true;
}

void WS2812FXDevice::reset() {
  for (int i = firstLED; i <= lastLED; i++) {
    ws2812FX->setPixelColor(i, RGBW_BLACK);
  }
  ws2812FX->show();
}

WS2812FX* WS2812FXDevice::getWS2812FX() { return ws2812FX; }

int WS2812FXDevice::getFirstLED() { return firstLED; }

int WS2812FXDevice::getlastLED() { return lastLED; }

int WS2812FXDevice::getNumLEDs() { return lastLED - firstLED + 1; }

bool WS2812FXDevice::isStopped() { return stopped; }

void WS2812FXDevice::setBrightness(byte b) {
  brightness = b;
  ws2812FX->setBrightness(brightness);
}

byte WS2812FXDevice::getBrightness() { return brightness; }

void WS2812FXDevice::show() {
  if (needsShow) {
    needsShow = false;
    ws2812FX->show();
  }
}