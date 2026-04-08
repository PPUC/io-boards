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
  PwmDevices(EventDispatcher *eventDispatcher)
      : HighPowerOffAware(eventDispatcher), _eventDispatcher(eventDispatcher) {
    eventDispatcher->addListener(this, EVENT_SOURCE_LIGHT);
    // Listening to solenoids and switches is added in HighPowerOffAware().
  }

  void registerSolenoid(byte p, byte n, byte pow, uint16_t minPT,
                        uint16_t maxPT, byte hP, uint16_t hPAT, byte fS);
  void registerFlasher(byte p, byte n, byte pow);
  void registerLamp(byte p, byte n, byte pow);

  void update();
  void off();
  void reset();

  void handleEvent(Event *event);

 private:
  int findRegisteredOutput(byte outputType, byte p, byte n) const;

  uint32_t _ms;

  byte port[MAX_PWM_OUTPUTS] = {0};
  byte number[MAX_PWM_OUTPUTS] = {0};
  byte power[MAX_PWM_OUTPUTS] = {0};
  uint16_t minPulseTime[MAX_PWM_OUTPUTS] = {0};
  uint16_t maxPulseTime[MAX_PWM_OUTPUTS] = {0};
  byte holdPower[MAX_PWM_OUTPUTS] = {0};
  uint16_t holdPowerActivationTime[MAX_PWM_OUTPUTS] = {0};
  byte fastSwitch[MAX_PWM_OUTPUTS] = {0};
  byte type[MAX_PWM_OUTPUTS] = {0};
  uint32_t activated[MAX_PWM_OUTPUTS] = {0};
  byte currentPower[MAX_PWM_OUTPUTS] = {0};
  bool scheduled[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchClosed[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchManagedActive[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchWaitForRelease[MAX_PWM_OUTPUTS] = {0};
  byte last = 0;
  EventDispatcher* _eventDispatcher = nullptr;

  void updateSolenoidOrFlasher(bool targetState, byte i);
  void handleFastSwitchEvent(bool switchClosed, byte i);
  void deactivateOutput(byte i);
};

#endif
