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

// How late the ROM's own copy of a fast-flip pulse may arrive and still be
// taken for one.
//
// A fast-flip output fires the moment its switch closes. The ROM sees the same
// switch a little later -- over the bus, through the host, through an emulated
// frame -- and fires the same coil itself. That copy is not a second request:
// obeyed, it fires a sling or a jet a second time, and its "off" drops a
// flipper the player is still holding. Within this long after the switch
// closed, a ROM "on" for the output is taken for that copy and ignored. It is
// an upper bound for the copy's delay, not a timing anything relies on: a ROM
// that fires the coil on its own -- a ball search, the coil test -- does so
// with no switch closing in front of it.
//
// Only for a flipper: an output its switch holds on (see
// FastSwitchOutputHolds). A sling or a jet obeys the ROM as before 0.4.1. With
// the ROM's "off" ignored, nothing but its switch opening or the maximum pulse
// time ended a sling's kick, and a chattering leaf turned one hit into a burst.
#ifndef FAST_SWITCH_ROM_COPY_WINDOW_MS
#define FAST_SWITCH_ROM_COPY_WINDOW_MS 300
#endif

// Whether a fast-flip output stays on for as long as its switch is closed - a
// flipper - rather than firing a pulse - a sling, a jet.
//
// A flipper's power winding is cut by its end-of-stroke switch, a single
// winding drops to hold power, and a separate hold winding has no maximum
// pulse time at all. A sling or a jet has a maximum pulse time and nothing
// else.
inline bool FastSwitchOutputHolds(uint16_t maxPulseTime,
                                  uint16_t holdPowerActivationTime,
                                  byte stopSwitch1, byte stopSwitch2) {
  return maxPulseTime == 0 || holdPowerActivationTime > 0 || stopSwitch1 > 0 ||
         stopSwitch2 > 0;
}

class PwmDevices : public HighPowerOffAware {
 public:
  // Constructor
  PwmDevices(EventDispatcher *eventDispatcher)
      : HighPowerOffAware(eventDispatcher), _eventDispatcher(eventDispatcher) {
    eventDispatcher->addListener(this, EVENT_SOURCE_LIGHT);
    eventDispatcher->addListener(this, EVENT_SOURCE_POWER_SCALE);
    // Listening to solenoids and switches is added in HighPowerOffAware().
  }

  void registerSolenoid(byte p, byte n, byte pow, uint16_t minPT,
                        uint16_t maxPT, byte hP, uint16_t hPAT, byte fS,
                        byte sS1 = 0, byte sS2 = 0,
                        byte outputType = PWM_TYPE_SOLENOID,
                        bool ignoreGO = false);
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
  byte scaledPower(byte i) const;
  void handlePowerScaleEvent(uint16_t motorNumber, byte scale);

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
  // Driven whether or not the game-on solenoid is asserted.
  bool ignoreGameOn[MAX_PWM_OUTPUTS] = {0};
  // How much of its power a motor runs at, set by the host. 255 is all of it.
  byte powerScale[MAX_PWM_OUTPUTS] = {0};
  uint32_t activated[MAX_PWM_OUTPUTS] = {0};
  byte currentPower[MAX_PWM_OUTPUTS] = {0};
  bool scheduled[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchClosed[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchManagedActive[MAX_PWM_OUTPUTS] = {0};
  bool fastSwitchWaitForRelease[MAX_PWM_OUTPUTS] = {0};
  // When the fast-flip switch last closed, for telling the ROM's copy of a
  // pulse from a request of its own. 0 means not since registration.
  uint32_t fastSwitchClosedAt[MAX_PWM_OUTPUTS] = {0};
  // Last seen state of each stop switch, for spotting the moment it closes.
  bool stopSwitchClosed[MAX_PWM_OUTPUTS][MAX_STOP_SWITCHES_PER_OUTPUT] = {{0}};
  // A stop switch closed while this output was on, and it stays off until every
  // stop switch is open again.
  bool stopEngaged[MAX_PWM_OUTPUTS] = {0};
  // How often the output fired again by itself since the button was pressed.
  byte stopRedrives[MAX_PWM_OUTPUTS] = {0};
  byte last = 0;
  EventDispatcher *_eventDispatcher = nullptr;

  void updateSolenoidOrFlasher(bool targetState, byte i);
  bool isRomCopyOfFastSwitchPulse(bool targetState, byte i) const;
  bool fastSwitchHoldsAnOutput(byte i) const;
  void noteFastSwitchState(bool switchClosed, byte i);
  void handleFastSwitchEvent(bool switchClosed, byte i);
  void handleStopSwitchEvent(byte switchNumber, bool switchClosed, byte i);
  void refreshStopSwitches(byte i);
  bool anyStopSwitchClosed(byte i) const;
  bool hasStopSwitch(byte i) const;
  void deactivateOutput(byte i);
};

#endif
