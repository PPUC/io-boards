/*
  PPUCBoardTypes.h.
  Created by Markus Kalkbrenner, 2026.

  What each board type is, in one place.

  Adding a board should mean editing this file and nothing else in the
  firmware: the type value and name come from the protocol (they are on the
  wire and shared with the host), and everything board-local - which
  subsystems exist, the switch matrix geometry, how many dedicated switches -
  lives in the table below.

  Before this, the same knowledge was spread across a switch statement in
  IOBoardController.cpp, an `if (controllerType == CONTROLLER_16_8_1)` guard
  around the whole of begin(), and constants in PPUC.h. A second board type
  would have had to find all three.

  Play more pinball!
*/

#ifndef PPUCBoardTypes_h
#define PPUCBoardTypes_h

#include <stdint.h>

#include "IODevices/SwitchMatrix.h"
#include "PPUCProtocolV2.h"

namespace ppuc {
namespace board {

// What a board can do. A board only builds the subsystems it declares, so an
// Opto_16 does not carry a PWM output stage it has no drivers for.
enum Capability : uint8_t {
  kCapNone = 0x00,
  kCapDedicatedSwitches = 0x01,  // direct switch inputs
  kCapPwmOutputs = 0x02,         // coils, lamps, flashers
  kCapSwitchMatrix = 0x04,       // scanned switch matrix
  kCapAddressableLeds = 0x08,    // WS2812 strings
};

// Everything board-local about one type.
struct Profile {
  uint8_t type = ppuc::v2::kBoardTypeUnknown;
  uint8_t capabilities = kCapNone;

  // Dedicated switch inputs, when kCapDedicatedSwitches is set.
  uint8_t switchBasePin = 0;
  uint8_t maxSwitches = 0;

  // Matrix geometry, when kCapSwitchMatrix is set. Ignored otherwise.
  SwitchMatrixProfile matrix = {0, 0, 0, 0};

  // Whether firmware for this board has been exercised on real hardware.
  //
  // Recorded rather than assumed: a board type can be declared here, build,
  // and report itself over the bus long before its outputs have ever been
  // driven correctly. The host uses this to decide whether an image is safe
  // to hand out automatically.
  bool validatedOnHardware = false;

  bool has(Capability cap) const { return (capabilities & cap) != 0; }
};

// IO_16_8_1: 16 inputs, 8 high-power outputs, one special output.
// The only board with hardware validation behind it.
constexpr Profile kIo16_8_1 = {
    ppuc::v2::kBoardTypeIo16_8_1,
    kCapDedicatedSwitches | kCapPwmOutputs | kCapSwitchMatrix |
        kCapAddressableLeds,
    /*switchBasePin*/ 3,
    /*maxSwitches*/ 16,
    /*matrix*/ {4, 8, 15, (1u << 4) | (1u << 8)},
    /*validatedOnHardware*/ true,
};

// Opto_16: 16 opto-isolated inputs and the special output, nothing else.
// Its inputs are on the same GPIOs as IO_16_8_1's, which is why the existing
// switch reader covers it unchanged.
constexpr Profile kOpto16 = {
    ppuc::v2::kBoardTypeOpto16,
    kCapDedicatedSwitches,
    /*switchBasePin*/ 3,
    /*maxSwitches*/ 16,
    /*matrix*/ {0, 0, 0, 0},
    /*validatedOnHardware*/ false,
};

// IO_16x8_matrix: 16 inputs and 8 signal outputs, intended for an original
// playfield harness.
//
// Its outputs run the opposite way to IO_16_8_1 - Out_1 is GPIO27 descending
// to Out_8 on GPIO19 - which is handled host-side by the port-to-GPIO map in
// config-tool, not here. What is not yet done is the matrix support itself.
constexpr Profile kIo16x8Matrix = {
    ppuc::v2::kBoardTypeIo16x8Matrix,
    kCapDedicatedSwitches | kCapPwmOutputs | kCapSwitchMatrix,
    /*switchBasePin*/ 3,
    /*maxSwitches*/ 16,
    /*matrix*/ {4, 8, 15, (1u << 4) | (1u << 8)},
    /*validatedOnHardware*/ false,
};

// Out_8x10: 8 high-side by 10 low-side, for driving an original lamp matrix.
// The lamp-matrix output stage does not exist in the firmware yet.
constexpr Profile kOut8x10 = {
    ppuc::v2::kBoardTypeOut8x10,
    kCapPwmOutputs,
    /*switchBasePin*/ 0,
    /*maxSwitches*/ 0,
    /*matrix*/ {0, 0, 0, 0},
    /*validatedOnHardware*/ false,
};

// The profile for the type this image was built for.
constexpr Profile profileFor(uint8_t type) {
  return type == ppuc::v2::kBoardTypeIo16_8_1      ? kIo16_8_1
         : type == ppuc::v2::kBoardTypeOpto16      ? kOpto16
         : type == ppuc::v2::kBoardTypeIo16x8Matrix ? kIo16x8Matrix
         : type == ppuc::v2::kBoardTypeOut8x10     ? kOut8x10
                                                   : Profile{};
}

// This image's own profile. PPUC_BOARD_TYPE comes from the build.
constexpr Profile self() { return profileFor(PPUC_BOARD_TYPE); }

}  // namespace board
}  // namespace ppuc

#endif
