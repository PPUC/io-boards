// Definitions for the native test build.
//
// Two groups of symbols live here:
//
//  1. The Arduino shim declared in test/stubs/Arduino.h - a test-driven clock
//     and a record of pin writes.
//
//  2. Link seams for firmware symbols that PwmDevices references but whose
//     real definitions would drag in the entire protocol and serial stack:
//     EventDispatcher and CrossLinkDebugger. The class *declarations* come
//     from the real headers, so the object layout is the production one; only
//     the definitions of the handful of methods actually reached are supplied
//     here.
//
// No production file changes for any of this.

#include "HardwareStubs.h"

#include <map>

#include "Arduino.h"
#include "EventDispatcher/CrossLinkDebugger.h"
#include "EventDispatcher/EventDispatcher.h"

namespace {

uint32_t g_millis = 0;
std::map<uint8_t, int> g_analogValues;
std::map<uint8_t, int> g_analogWrites;
std::map<uint8_t, int> g_digitalValues;
std::map<uint16_t, bool> g_switchStates;

}  // namespace

namespace stubs {

void SetMillis(uint32_t ms) { g_millis = ms; }
void AdvanceMillis(uint32_t ms) { g_millis += ms; }
uint32_t CurrentMillis() { return g_millis; }

int AnalogValue(uint8_t pin) {
  const auto it = g_analogValues.find(pin);
  return it == g_analogValues.end() ? 0 : it->second;
}

bool WasAnalogWritten(uint8_t pin) {
  return g_analogValues.find(pin) != g_analogValues.end();
}

int AnalogWriteCount(uint8_t pin) {
  const auto it = g_analogWrites.find(pin);
  return it == g_analogWrites.end() ? 0 : it->second;
}

void SetSwitchState(uint16_t number, bool closed) {
  g_switchStates[number] = closed;
}

bool SwitchStateForTest(uint16_t number) {
  const auto it = g_switchStates.find(number);
  return it == g_switchStates.end() ? false : it->second;
}

void Reset() {
  g_millis = 0;
  g_analogValues.clear();
  g_analogWrites.clear();
  g_digitalValues.clear();
  g_switchStates.clear();
}

}  // namespace stubs

// --- Arduino shim -----------------------------------------------------------

HardwareSerial Serial;
HardwareSerial Serial1;

uint32_t millis() { return g_millis; }
uint32_t micros() { return g_millis * 1000u; }
void delay(uint32_t ms) { g_millis += ms; }
void delayMicroseconds(uint32_t) {}

void pinMode(uint8_t, uint8_t) {}

void digitalWrite(uint8_t pin, uint8_t value) { g_digitalValues[pin] = value; }

int digitalRead(uint8_t pin) {
  const auto it = g_digitalValues.find(pin);
  return it == g_digitalValues.end() ? 0 : it->second;
}

void analogWrite(uint8_t pin, int value) {
  g_analogValues[pin] = value;
  g_analogWrites[pin]++;
}

int analogRead(uint8_t) { return 0; }

// --- Link seams -------------------------------------------------------------

// PwmDevices and HighPowerOffAware register themselves and query switch state.
// Nothing else on EventDispatcher is reached from the code under test.
EventDispatcher::EventDispatcher() {}

void EventDispatcher::addListener(EventListener*) {}
void EventDispatcher::addListener(EventListener*, char) {}

bool EventDispatcher::getSwitchState(uint16_t number) const {
  return stubs::SwitchStateForTest(number);
}

// CrossLinkDebugger is inert in the shipped firmware - both listener
// registrations in main.cpp are commented out, so `active` is never set and
// debug() is a no-op behind that flag. Kept no-op here for the same reason.
bool CrossLinkDebugger::active = false;

void CrossLinkDebugger::debug(const char*, ...) {}
