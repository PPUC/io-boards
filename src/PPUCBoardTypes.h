/*
  PPUCBoardTypes.h.
  Created by Markus Kalkbrenner, 2026.

  What each board type is, in one place.

  Adding a board should mean editing this file and nothing else in the
  firmware: the type value and name come from the protocol (they are on the
  wire and shared with the host), and everything board-local - which
  subsystems exist, which GPIO does what, the matrix geometries - lives in the
  table below.

  Before this, the same knowledge was spread across a switch statement in
  IOBoardController.cpp, an `if (controllerType == CONTROLLER_16_8_1)` guard
  around the whole of begin(), and constants in PPUC.h. A second board type
  would have had to find all three.

  This header is shared with the host. libppuc stages it next to
  PPUCProtocolV2.h and uses the same table to refuse a configuration that puts
  a device on a pin its board does not have, so it must stay free of anything
  that only exists on the RP2040: no Arduino.h, no pico-sdk, nothing but
  <stdint.h> and the protocol header.

  The GPIO numbers were traced from the KiCad schematics of each board
  (config-tool/boards/trace_kicad_gpio.py), not copied from one board to the
  next. They differ in ways that matter - see the notes on each profile.

  Play more pinball!
*/

#ifndef PPUCBoardTypes_h
#define PPUCBoardTypes_h

#include <stdint.h>

#include "PPUCProtocolV2.h"

// The 4-column matrix scanned on IO_16_8_1's own inputs. Columns are driven on
// `columns` consecutive GPIOs starting at columnsBasePin, and the rows sit
// directly below them.
struct SwitchMatrixProfile {
  uint8_t columns;
  uint8_t maxRows;
  uint8_t columnsBasePin;
  uint16_t supportedRowsMask;
};

namespace ppuc {
namespace board {

constexpr uint8_t kMaxStrobes = 8;
constexpr uint8_t kMaxLampColumns = 8;
constexpr uint8_t kMaxLampRows = 10;

// A switch matrix with its own strobe drivers, as on IO_16x8_matrix: every
// input is a return, and the strobes are separate outputs.
//
// The strobe GPIOs are listed rather than given as a base and a count because
// they are neither contiguous nor ascending on the real board.
struct StrobedMatrixProfile {
  uint8_t strobes;        // 0 when the board has no such matrix
  uint8_t strobeBasePin;  // lowest strobe GPIO
  uint8_t strobeSpan;     // GPIOs from the base through the highest strobe,
                          // counting any that are not strobes
  uint8_t strobePins[kMaxStrobes];  // GPIO per column, in connector order
  uint8_t returnsBasePin;
  uint8_t maxReturns;
};

// A lamp matrix: high-side columns by low-side rows, as on Out_8x10.
struct LampMatrixProfile {
  uint8_t columns;  // 0 when the board has no lamp matrix
  uint8_t rows;
  uint8_t columnPins[kMaxLampColumns];  // high side, Hi_1 first
  uint8_t rowPins[kMaxLampRows];        // low side, Lo_1 first
};

// What a board can do. A board only builds the subsystems it declares, so an
// Opto_16 does not carry a PWM output stage it has no drivers for.
enum Capability : uint8_t {
  kCapNone = 0x00,
  kCapDedicatedSwitches = 0x01,    // direct switch inputs
  kCapPwmOutputs = 0x02,           // coils, lamps, flashers
  kCapSwitchMatrix = 0x04,         // 4-column matrix scanned on the inputs
  kCapAddressableLeds = 0x08,      // WS2812 string on the special output
  kCapStrobedSwitchMatrix = 0x10,  // matrix with dedicated strobe drivers
  kCapLampMatrix = 0x20,           // strobed lamp matrix
};

// GPIOs lo..hi inclusive as a bit mask.
constexpr uint32_t pinRange(uint8_t lo, uint8_t hi) {
  return ((hi >= 31 ? 0xFFFFFFFFu : ((1u << (hi + 1)) - 1u)) &
          ~((1u << lo) - 1u));
}

constexpr uint32_t pinBit(uint8_t pin) { return pin < 32 ? (1u << pin) : 0u; }

// Two GPIOs sixteen apart are driven by the same RP2040 PWM channel: the slice
// is (gpio / 2) % 8 and the channel gpio % 2. Put both in PWM mode and a duty
// cycle written to one appears on the other. On IO_16_8_1 that pairs input 1
// (GPIO 3) with high-power output 1 (GPIO 19), so a lamp dimmed on the input
// would fire the coil.
constexpr bool sharesPwmChannel(uint8_t a, uint8_t b) {
  return a != b && (a & 0x0F) == (b & 0x0F);
}

// Everything board-local about one type.
struct Profile {
  uint8_t type = ppuc::v2::kBoardTypeUnknown;
  uint8_t capabilities = kCapNone;

  // GPIOs usable as dedicated switch inputs.
  uint32_t inputPins = 0;
  // GPIOs a coil, flasher, lamp, motor or shaker may be registered on.
  uint32_t pwmPins = 0;
  // GPIOs that carry a lamp matrix line or a lamp wired straight to one
  // output. Driven by the lamp matrix stage, never by PWM.
  uint32_t lampPins = 0;
  // GPIOs the watchdog forces low when the main loop stalls or the host goes
  // quiet. Everything that can put current through a load.
  uint32_t safeOffPins = 0;
  // The special output, a WS2812 data line. On every board so far.
  uint8_t ledPin = 0;

  SwitchMatrixProfile matrix = {0, 0, 0, 0};
  StrobedMatrixProfile strobedMatrix = {0, 0, 0, {0}, 0, 0};
  LampMatrixProfile lampMatrix = {0, 0, {0}, {0}};

  constexpr bool has(Capability cap) const { return (capabilities & cap) != 0; }

  constexpr bool allowsSwitch(uint8_t pin) const {
    return has(kCapDedicatedSwitches) && (inputPins & pinBit(pin)) != 0;
  }

  constexpr bool allowsPwm(uint8_t pin) const {
    return has(kCapPwmOutputs) && (pwmPins & pinBit(pin)) != 0;
  }

  constexpr bool allowsDirectLamp(uint8_t pin) const {
    return has(kCapLampMatrix) && (lampPins & pinBit(pin)) != 0;
  }

  constexpr bool allowsLedString(uint8_t pin) const {
    return has(kCapAddressableLeds) && pin == ledPin;
  }

  constexpr bool isStrobePin(uint8_t pin) const {
    for (uint8_t i = 0; i < strobedMatrix.strobes; i++) {
      if (strobedMatrix.strobePins[i] == pin) {
        return true;
      }
    }
    return false;
  }

  // Whether this board's firmware has been exercised on real hardware. Kept in
  // the protocol header, not here, because the host is what acts on it - it
  // decides whether to flash a board unattended.
  constexpr bool validatedOnHardware() const {
    return ppuc::v2::BoardTypeValidatedOnHardware(type);
  }
};

// The eight outputs IO_16_8_1 and IO_16x8_matrix carry on the same GPIOs.
// GPIO 25 is the on-board LED and sits in the middle of them.
constexpr uint32_t kEightOutputs = pinRange(19, 24) | pinRange(26, 27);

// IO_16_8_1: 16 inputs, 8 high-power outputs, one special output.
// The only board with hardware validation behind it.
//
// The inputs double as low-power outputs, which is why pwmPins covers them
// too. Only the eight high-power outputs are forced off by the watchdog; an
// input used as an output can sink 2 mA.
constexpr Profile kIo16_8_1 = {
    ppuc::v2::kBoardTypeIo16_8_1,
    kCapDedicatedSwitches | kCapPwmOutputs | kCapSwitchMatrix |
        kCapAddressableLeds,
    /*inputPins*/ pinRange(3, 18),
    /*pwmPins*/ pinRange(3, 18) | kEightOutputs,
    /*lampPins*/ 0,
    /*safeOffPins*/ kEightOutputs,
    /*ledPin*/ 29,
    /*matrix*/ {4, 8, 15, (1u << 4) | (1u << 8)},
    /*strobedMatrix*/ {0, 0, 0, {0}, 0, 0},
    /*lampMatrix*/ {0, 0, {0}, {0}},
};

// Opto_16: 16 opto inputs plus the special output, nothing else.
// Its inputs are on the same GPIOs as IO_16_8_1's, which is why the existing
// switch reader covers it unchanged. The 16 transmitter LED drivers are wired
// to the supply, not to the RP2040, so there is nothing to drive.
//
// The special output is the WS2812 connector every board carries, so an
// Opto_16 can drive an LED string even though it has no PWM output stage. That
// matters to whoever is placing boards: a string does not need a board with
// drivers on it, and a board is a board's worth of space under a playfield.
constexpr Profile kOpto16 = {
    ppuc::v2::kBoardTypeOpto16,
    kCapDedicatedSwitches | kCapAddressableLeds,
    /*inputPins*/ pinRange(3, 18),
    /*pwmPins*/ 0,
    /*lampPins*/ 0,
    /*safeOffPins*/ 0,
    /*ledPin*/ 29,
    /*matrix*/ {0, 0, 0, 0},
    /*strobedMatrix*/ {0, 0, 0, {0}, 0, 0},
    /*lampMatrix*/ {0, 0, {0}, {0}},
};

// IO_16x8_matrix: 16 inputs and 8 signal outputs, for the switch matrix of an
// original playfield harness. Also usable as 16 direct inputs plus 8 low-power
// outputs.
//
// Its outputs run the opposite way to IO_16_8_1 - Out_1 is GPIO 27 descending
// to Out_8 on GPIO 19 - and skip GPIO 25, the on-board LED. Each one is an NPN
// stage with a 1 k pull-up to 5 V, so a high GPIO pulls the line to ground:
// the strobe is active-low on the connector and active-high on the pin.
//
// It deliberately has no kCapSwitchMatrix. That is IO_16_8_1's 4-column scan
// on the inputs, with different PIO programs; this board strobes its own
// outputs instead.
constexpr Profile kIo16x8Matrix = {
    ppuc::v2::kBoardTypeIo16x8Matrix,
    kCapDedicatedSwitches | kCapPwmOutputs | kCapStrobedSwitchMatrix |
        kCapAddressableLeds,
    /*inputPins*/ pinRange(3, 18),
    /*pwmPins*/ kEightOutputs,
    /*lampPins*/ 0,
    /*safeOffPins*/ kEightOutputs,
    /*ledPin*/ 29,
    /*matrix*/ {0, 0, 0, 0},
    /*strobedMatrix*/
    {8, 19, 9, {27, 26, 24, 23, 22, 21, 20, 19}, 3, 16},
    /*lampMatrix*/ {0, 0, {0}, {0}},
};

// Out_8x10: 8 high-side by 10 low-side switches, for the lamp matrix of an
// original playfield fitted with LEDs. Also usable as plain high-side and
// low-side outputs.
//
// Both rows of switches are on when their GPIO is high. Hi_1 is GPIO 24
// descending to Hi_8 on GPIO 17, Lo_1 is GPIO 12 descending to Lo_10 on GPIO 3.
//
// No kCapPwmOutputs, on purpose. Lo_5..Lo_10 share their PWM channels with
// Hi_1..Hi_6 (see sharesPwmChannel), so dimming one through analogWrite would
// drive the other. Every output on this board goes through the lamp matrix
// stage instead, which does not use the PWM hardware at all.
constexpr Profile kOut8x10 = {
    ppuc::v2::kBoardTypeOut8x10,
    kCapLampMatrix | kCapAddressableLeds,
    /*inputPins*/ 0,
    /*pwmPins*/ 0,
    /*lampPins*/ pinRange(3, 12) | pinRange(17, 24),
    /*safeOffPins*/ pinRange(3, 12) | pinRange(17, 24),
    /*ledPin*/ 29,
    /*matrix*/ {0, 0, 0, 0},
    /*strobedMatrix*/ {0, 0, 0, {0}, 0, 0},
    /*lampMatrix*/
    {8,
     10,
     {24, 23, 22, 21, 20, 19, 18, 17},
     {12, 11, 10, 9, 8, 7, 6, 5, 4, 3}},
};

// The profile for a board type. Unknown types get an empty profile that allows
// nothing.
constexpr Profile profileFor(uint8_t type) {
  return type == ppuc::v2::kBoardTypeIo16_8_1       ? kIo16_8_1
         : type == ppuc::v2::kBoardTypeOpto16       ? kOpto16
         : type == ppuc::v2::kBoardTypeIo16x8Matrix ? kIo16x8Matrix
         : type == ppuc::v2::kBoardTypeOut8x10      ? kOut8x10
                                                    : Profile{};
}

// The firmware's own profile, ppuc::board::self(), is in PPUC.h: it depends
// on the build's PPUC_BOARD_TYPE, and the host has no "own" board.

}  // namespace board
}  // namespace ppuc

#endif
