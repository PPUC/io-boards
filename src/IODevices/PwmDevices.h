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

// Switches that stop an output, per output.
//
// Two is what the hardware asks for: a flipper has one end-of-stroke contact,
// and a motor-driven assembly has one switch at each end of its travel. Raise
// it if something turns up with more.
#ifndef MAX_STOP_SWITCHES_PER_OUTPUT
#define MAX_STOP_SWITCHES_PER_OUTPUT 2
#endif

// The maximum pulse time a coil with a stop switch gets when it was configured
// with none.
//
// A coil that a switch has to stop is a flipper power winding, driven at full
// power by a button a player can hold for minutes. Its end-of-stroke contact
// can be unplugged or misadjusted, and then nothing else ends it. WPC never let
// that winding run longer than 40 ms whatever the contact did, so neither does
// the board, whatever the host sent.
#ifndef STOP_SWITCHED_MAX_PULSE_MS
#define STOP_SWITCHED_MAX_PULSE_MS 40
#endif

// How often a switch-driven output fires again by itself during one press.
//
// When a stop switch opens while the button is still held - a ball has pushed
// the flipper finger down - the board fires the power winding again. Each of
// those is bounded by the maximum pulse time, but their number was not: a
// chattering or misadjusted end-of-stroke contact would keep a full-power
// winding going for as long as the button is held. After this many, the
// output waits for the button to be released, and the hold winding keeps
// doing its job meanwhile.
#ifndef MAX_STOP_REDRIVES_PER_PRESS
#define MAX_STOP_REDRIVES_PER_PRESS 5
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
                        uint16_t maxPT, byte hP, uint16_t hPAT, byte fS,
                        byte sS1 = 0, byte sS2 = 0,
                        byte outputType = PWM_TYPE_SOLENOID);
  void registerFlasher(byte p, byte n, byte pow);
  void registerLamp(byte p, byte n, byte pow);

  void update();
  void off();
  void reset();

  // Overriding only the Event* overload would hide the ConfigEvent* one for
  // anyone holding a PwmDevices* directly. Virtual dispatch through
  // EventListener* still found it, so this was invisible in production and only
  // bit callers -- including tests -- that use the concrete type.
  using HighPowerOffAware::handleEvent;
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
  // Switches that cut this output the moment they close. A flipper's EOS, or
  // the switch at the end of a motor's travel. 0 means unused.
  byte stopSwitch[MAX_PWM_OUTPUTS][MAX_STOP_SWITCHES_PER_OUTPUT] = {{0}};
  byte type[MAX_PWM_OUTPUTS] = {0};
  uint32_t activated[MAX_PWM_OUTPUTS] = {0};
  byte currentPower[MAX_PWM_OUTPUTS] = {0};
  bool scheduled[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchClosed[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchManagedActive[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchWaitForRelease[MAX_PWM_OUTPUTS] = {0};
  // Last seen state of each stop switch, for spotting the moment it closes.
  bool stopSwitchClosed[MAX_PWM_OUTPUTS][MAX_STOP_SWITCHES_PER_OUTPUT] = {{0}};
  // A stop switch closed while this output was on, and it stays off until every
  // stop switch is open again.
  bool stopEngaged[MAX_PWM_OUTPUTS] = {0};
  // How often the output fired again by itself since the button was pressed.
  byte stopRedrives[MAX_PWM_OUTPUTS] = {0};
  byte last = 0;
  EventDispatcher* _eventDispatcher = nullptr;

  void updateSolenoidOrFlasher(bool targetState, byte i);
  void handleFastSwitchEvent(bool switchClosed, byte i);
  void handleStopSwitchEvent(byte switchNumber, bool switchClosed, byte i);
  void refreshStopSwitches(byte i);
  bool anyStopSwitchClosed(byte i) const;
  bool hasStopSwitch(byte i) const;
  void deactivateOutput(byte i);
};

#endif
