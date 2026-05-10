#include "WS2812FXEffect.h"

void WS2812FXEffect::setDevice(EffectDevice *effectDevice) {
  Effect::setDevice(effectDevice);
  ws2812FX = (WS2812FX *)((WS2812FXDevice *)device)->getWS2812FX();
}

void WS2812FXEffect::start(int r) {
  Effect::start(r);
  ws2812FX->getSegment(segment)->mode = mode;
  ws2812FX->getSegment(segment)->colors[0] = colors[0];
  ws2812FX->getSegment(segment)->colors[1] = colors[1];
  ws2812FX->getSegment(segment)->colors[2] = colors[2];
  ws2812FX->getSegment(segment)->speed = speed;
  ws2812FX->getSegment(segment)->options = options;
  ws2812FX->resetSegmentRuntime(segment);
}

void WS2812FXEffect::stop() {
  Effect::stop();
}

void WS2812FXEffect::terminate() {
  blackoutSegment();
  Effect::terminate();
}

void WS2812FXEffect::update() {
  // Don't call service() here!

  if (duration && duration < ms) {
    stop();
  }
}

void WS2812FXEffect::blackoutSegment() {
  ws2812FX->getSegment(segment)->mode = FX_MODE_STATIC;
  ws2812FX->getSegment(segment)->colors[0] = RGBW_BLACK;
  ws2812FX->getSegment(segment)->speed = 1;
  ws2812FX->getSegment(segment)->options = NO_OPTIONS;
  ws2812FX->resetSegmentRuntime(segment);
  ws2812FX->service();
}
