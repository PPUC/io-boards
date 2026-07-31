#pragma once

// Test-side control over the Arduino shim.
//
// The firmware calls millis() and analogWrite() directly. Rather than change
// production code to inject a clock or a GPIO interface, the native test build
// links against a shim whose behaviour a test drives through this API.

#include <cstdint>
#include <map>

namespace stubs {

// Controls the value millis() returns. Time only moves when a test says so, so
// pulse-timing assertions are exact rather than schedule-dependent.
void SetMillis(uint32_t ms);
void AdvanceMillis(uint32_t ms);
uint32_t CurrentMillis();

// Last value written to a pin by analogWrite(), or 0 if never written.
// PwmDevices drives coil power through analogWrite, so this is how a test
// observes whether an output is energised and at what power.
int AnalogValue(uint8_t pin);
bool WasAnalogWritten(uint8_t pin);

// Number of analogWrite() calls to a pin, so a test can distinguish "still on"
// from "turned off and on again".
int AnalogWriteCount(uint8_t pin);

// Controls what EventDispatcher::getSwitchState() reports. PwmDevices polls
// this every update() for fast-switch solenoids, so a test drives the physical
// switch through here.
void SetSwitchState(uint16_t number, bool closed);
bool SwitchStateForTest(uint16_t number);

// Clears all recorded pin state, switch state, and resets the clock. Call
// between tests.
void Reset();

}  // namespace stubs
