#include "WS2812FXEffect.h"

void WS2812FXEffect::setDevice(EffectDevice *effectDevice) {
  Effect::setDevice(effectDevice);
  ws2812FX = (WS2812FX *)((WS2812FXDevice *)device)->getWS2812FX();
}

void WS2812FXEffect::start(int r) {
  Effect::start();
  device->on();
  if (segment == 255) {
    ws2812FX->setSegment(getFirstSegment(), getFirstLED(), getlastLED(), mode,
                         colors, speed, options);
    ws2812FX->resetSegmentRuntime(getFirstSegment());
  } else {
    ws2812FX->getSegment(segment)->mode = mode;
    ws2812FX->getSegment(segment)->colors[0] = colors[0];
    ws2812FX->getSegment(segment)->colors[1] = colors[1];
    ws2812FX->getSegment(segment)->colors[2] = colors[2];
    ws2812FX->getSegment(segment)->speed = speed;
    ws2812FX->getSegment(segment)->options = options;
    ws2812FX->resetSegmentRuntime(segment);
  }
}

void WS2812FXEffect::stop() {
  device->off();
  Effect::stop();
}

void WS2812FXEffect::update() {
  // Don't call service() here!

  if (duration && duration < ms) {
    stop();
  }
}

int WS2812FXEffect::getFirstLED() {
  return ((WS2812FXDevice *)device)->getFirstLED();
}

int WS2812FXEffect::getlastLED() {
  return ((WS2812FXDevice *)device)->getlastLED();
}

int WS2812FXEffect::getNumLEDs() {
  return ((WS2812FXDevice *)device)->getNumLEDs();
}

int WS2812FXEffect::getFirstSegment() {
  return ((WS2812FXDevice *)device)->getFirstSegment();
}

int WS2812FXEffect::getLastSegment() {
  return ((WS2812FXDevice *)device)->getLastSegment();
}

int WS2812FXEffect::getNumSegments() {
  return ((WS2812FXDevice *)device)->getNumSegments();
}
