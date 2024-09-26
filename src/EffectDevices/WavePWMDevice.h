/*
  WavePWMDevice.h
  Created by Markus Kalkbrenner, 2021.

  Play more pinball!
*/

#ifndef WavePWMDevice_h
#define WavePWMDevice_h

#include <Arduino.h>
#include <WavePWM.h>

#include "../HighPowerOffAware.h"
#include "EffectDevice.h"

class WavePWMDevice : public EffectDevice, public HighPowerOffAware {
 public:
  WavePWMDevice(int pin) {
    this->wavePWM = new WavePWM();
    this->pin = pin;
    pinMode(pin, OUTPUT);
  }

  WavePWMDevice(int pin, EventDispatcher* eventDispatcher)
      : HighPowerOffAware(eventDispatcher) {
    this->wavePWM = new WavePWM();
    this->pin = pin;
    pinMode(pin, OUTPUT);
  }

  void on();

  void reset();

  virtual void setPWM(uint8_t pwm);

  uint8_t getPWM();

  WavePWM* getWavePWM();

  void handleEvent(Event* event);

 protected:
  WavePWM* wavePWM;

  int pin;
  uint8_t currentPWM = 0;
};

#endif
