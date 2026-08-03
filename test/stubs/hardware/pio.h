#pragma once

// Host-side model of the RP2040 PIO allocator surface.
//
// This exists so PioAllocation.h can be tested on a laptop. It models only
// what the allocator touches: two blocks, four state machines each, and a
// finite instruction memory. That is enough to exercise the parts worth
// testing - all-or-nothing rollback, co-location of a device's state machines
// on one block, and whether release actually gives the resources back.
//
// Reachable only through the -I path in the `native` PlatformIO environment;
// on real hardware the Pico SDK header wins.

#include <cstdint>
#include <cstring>

using uint = unsigned int;

#define NUM_PIOS 2
#define PIO_STUB_NUM_SMS 4
#define PIO_STUB_INSTRUCTION_SLOTS 32

struct pio_program {
  const uint16_t* instructions;
  uint8_t length;
  int8_t origin;
};
using pio_program_t = struct pio_program;

struct pio_hw_stub {
  uint32_t irq;
  bool smClaimed[PIO_STUB_NUM_SMS];
  bool smEnabled[PIO_STUB_NUM_SMS];
  uint instructionsUsed;
};
using PIO = pio_hw_stub*;

namespace pio_stub {

inline pio_hw_stub blocks[NUM_PIOS];

// Test control: wipe both blocks back to power-on state.
inline void reset() {
  for (uint i = 0; i < NUM_PIOS; i++) {
    std::memset(&blocks[i], 0, sizeof(blocks[i]));
  }
}

inline uint instructionsUsed(uint block) { return blocks[block].instructionsUsed; }

inline uint claimedStateMachines(uint block) {
  uint n = 0;
  for (uint i = 0; i < PIO_STUB_NUM_SMS; i++) {
    if (blocks[block].smClaimed[i]) n++;
  }
  return n;
}

inline uint totalClaimedStateMachines() {
  uint n = 0;
  for (uint i = 0; i < NUM_PIOS; i++) n += claimedStateMachines(i);
  return n;
}

inline uint totalInstructionsUsed() {
  uint n = 0;
  for (uint i = 0; i < NUM_PIOS; i++) n += blocks[i].instructionsUsed;
  return n;
}

}  // namespace pio_stub

inline PIO pio_get_instance(uint instance) { return &pio_stub::blocks[instance]; }

inline bool pio_can_add_program(PIO pio, const pio_program_t* program) {
  return pio->instructionsUsed + program->length <= PIO_STUB_INSTRUCTION_SLOTS;
}

inline uint pio_add_program(PIO pio, const pio_program_t* program) {
  const uint offset = pio->instructionsUsed;
  pio->instructionsUsed += program->length;
  return offset;
}

inline void pio_remove_program(PIO pio, const pio_program_t* program,
                               uint offset) {
  (void)offset;
  pio->instructionsUsed -= program->length;
}

inline int pio_claim_unused_sm(PIO pio, bool required) {
  (void)required;
  for (uint i = 0; i < PIO_STUB_NUM_SMS; i++) {
    if (!pio->smClaimed[i]) {
      pio->smClaimed[i] = true;
      return static_cast<int>(i);
    }
  }
  return -1;
}

inline void pio_sm_unclaim(PIO pio, uint sm) { pio->smClaimed[sm] = false; }

inline void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
  pio->smEnabled[sm] = enabled;
}

inline void pio_remove_program_and_unclaim_sm(const pio_program_t* program,
                                              PIO pio, uint sm, uint offset) {
  pio_remove_program(pio, program, offset);
  pio_sm_unclaim(pio, sm);
}
