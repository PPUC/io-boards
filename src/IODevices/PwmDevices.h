/*
  PwmDevices.h
  Created by Markus Kalkbrenner, 2022.

  Play more pinball!
*/
#ifndef OUTPUT_PwmDevices_h
#define OUTPUT_PwmDevices_h

#include <Arduino.h>

#include "../HighPowerOffAware.h"

#ifndef MAX_PWM_OUTPUTS
#define MAX_PWM_OUTPUTS 16
#endif

class PwmDevices : public HighPowerOffAware {
 public:
  // Constructor
  PwmDevices(EventDispatcher *eventDispatcher): HighPowerOffAware(eventDispatcher) {
    eventDispatcher->addListener(this, EVENT_SOURCE_LIGHT);

    // Adjust PWM properties if needed.
    // analogWriteFreq(5000);
    // analogWriteRange(65535);
    // analogWriteResolution(16);
  }

  void registerSolenoid(byte p, byte n, byte pow, uint16_t minPT,
                        uint16_t maxPT, byte hP, uint16_t hPAT, byte fS);
  void registerFlasher(byte p, byte n, byte pow);
  void registerLamp(byte p, byte n, byte pow);

  void update();
  void reset();

  void handleEvent(Event *event);

 private:
  unsigned long _ms;

  byte port[MAX_PWM_OUTPUTS] = {0};
  byte number[MAX_PWM_OUTPUTS] = {0};
  byte power[MAX_PWM_OUTPUTS] = {0};
  uint16_t minPulseTime[MAX_PWM_OUTPUTS] = {0};
  uint16_t maxPulseTime[MAX_PWM_OUTPUTS] = {0};
  byte holdPower[MAX_PWM_OUTPUTS] = {0};
  uint16_t holdPowerActivationTime[MAX_PWM_OUTPUTS] = {0};
  byte fastSwitch[MAX_PWM_OUTPUTS] = {0};
  byte type[MAX_PWM_OUTPUTS] = {0};
  unsigned long activated[MAX_PWM_OUTPUTS] = {0};
  bool scheduled[MAX_PWM_OUTPUTS] = {0};
  byte last = 0;

  void updateSolenoidOrFlasher(bool targetState, byte i);
};

#endif
