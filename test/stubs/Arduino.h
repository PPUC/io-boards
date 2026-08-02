#pragma once

// Minimal Arduino shim for native (host) unit tests.
//
// This exists so that PwmDevices and its dependencies compile on a laptop and
// in CI without an RP2040. It provides only what the firmware under test
// actually uses - see test/stubs/HardwareStubs.h for the parts a test drives.
//
// No production file includes this; it is reachable only through the -I path
// in the `native` PlatformIO environment.

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

using byte = uint8_t;
using word = uint16_t;
using boolean = bool;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LED_BUILTIN 25

// Deliberately NO min/max macros here.
//
// Arduino traditionally defines them as function-like macros, which is hostile
// to the C++ standard library: libstdc++ headers call std::min(...) internally,
// and a `min` macro rewrites that to std::std::min(...). It compiled on macOS
// (libc++ does not hit those paths) and failed on Linux with libstdc++ - the
// kind of divergence only CI finds.
//
// Nothing in the code under test calls bare min()/max(), so the safest shim is
// none at all. If a future device does need them, add inline function
// templates rather than macros:
//
//   template <typename T, typename U>
//   constexpr auto min(T a, U b) -> decltype(a < b ? a : b) { ... }

// --- Time -------------------------------------------------------------------
// Driven by the test rather than by a real clock, so pulse timing can be
// asserted exactly. See ArduinoStub::setMillis / advanceMillis.
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

// --- GPIO -------------------------------------------------------------------
// Writes are recorded so a test can assert what a pin was actually driven to.
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
void analogWrite(uint8_t pin, int value);
int analogRead(uint8_t pin);

// --- Multicore --------------------------------------------------------------
// MultiCoreCrossLink guards its queues by core. Native tests are single
// threaded and always report core 0; the cross-link itself is not exercised by
// the PWM tests.
inline unsigned int get_core_num() { return 0; }

// --- Serial -----------------------------------------------------------------
// Only the declaration is needed: EventDispatcher holds a HardwareSerial*, and
// nothing under test dereferences it.
class HardwareSerial {
 public:
  void begin(unsigned long) {}
  void end() {}
  int available() { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t*, size_t) { return 0; }
  void flush() {}
  explicit operator bool() const { return false; }
};

extern HardwareSerial Serial;
extern HardwareSerial Serial1;
