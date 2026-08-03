/*
  PioAllocation.h.
  Created by Markus Kalkbrenner, 2026.

  Dynamic allocation of PIO state machines and program space.

  State machines used to be handed out by hand: the switch matrix took 0 and 1,
  the dedicated switch reader took 2 with a comment saying why, and everything
  lived on pio0. That reserved two of the eight state machines on every board
  whether or not it had a matrix configured, left pio1 completely unused, and
  meant adding any new PIO consumer required auditing the whole allocation by
  hand.

  Play more pinball!
*/

#ifndef PioAllocation_h
#define PioAllocation_h

#include "hardware/pio.h"

// A PIO program together with the state machine that runs it.
//
// Set `program` before claiming. Everything else belongs to the allocator.
struct PioSlot {
  const pio_program_t* program = nullptr;
  PIO pio = nullptr;
  uint sm = 0;
  uint offset = 0;
  bool claimed = false;
};

// Gives back everything a successful claim took. Slots that were never
// claimed are skipped, so this is safe to call unconditionally on teardown.
inline void pioReleaseSlots(PioSlot* slots, uint count) {
  for (uint i = 0; i < count; i++) {
    if (!slots[i].claimed) {
      continue;
    }
    pio_sm_set_enabled(slots[i].pio, slots[i].sm, false);
    pio_remove_program_and_unclaim_sm(slots[i].program, slots[i].pio,
                                      slots[i].sm, slots[i].offset);
    slots[i].pio = nullptr;
    slots[i].sm = 0;
    slots[i].offset = 0;
    slots[i].claimed = false;
  }
}

// Tries to place every slot on one specific PIO block. All or nothing.
inline bool pioClaimSlotsOn(PIO pio, PioSlot* slots, uint count) {
  uint added = 0;
  uint claimed = 0;

  while (added < count && pio_can_add_program(pio, slots[added].program)) {
    slots[added].pio = pio;
    slots[added].offset = pio_add_program(pio, slots[added].program);
    added++;
  }

  if (added == count) {
    while (claimed < count) {
      const int sm = pio_claim_unused_sm(pio, false);
      if (sm < 0) {
        break;
      }
      slots[claimed].sm = static_cast<uint>(sm);
      claimed++;
    }
  }

  if (added == count && claimed == count) {
    for (uint i = 0; i < count; i++) {
      slots[i].claimed = true;
    }
    return true;
  }

  // A partial claim is a failed claim. Hand back every piece so the next
  // block, and the next device to ask, sees the machine exactly as it was.
  for (uint i = 0; i < claimed; i++) {
    pio_sm_unclaim(pio, slots[i].sm);
  }
  for (uint i = 0; i < added; i++) {
    pio_remove_program(pio, slots[i].program, slots[i].offset);
  }
  for (uint i = 0; i < count; i++) {
    slots[i].pio = nullptr;
    slots[i].sm = 0;
    slots[i].offset = 0;
    slots[i].claimed = false;
  }
  return false;
}

// Claims `count` state machines and loads their programs, all on the *same*
// PIO block, trying each block in turn.
//
// Co-location is a hardware requirement, not tidiness. A GPIO's function
// select can only point at one PIO block at a time, and the switch matrix
// drives the column pins from one state machine while a second state machine
// waits on those same column pins. Split across blocks, the second
// pio_gpio_init() would take the column pins away from the state machine
// driving them.
//
// Returns false when no single block can host all of them, having left the
// hardware exactly as it found it.
inline bool pioClaimSlots(PioSlot* slots, uint count) {
  for (uint i = 0; i < NUM_PIOS; i++) {
    if (pioClaimSlotsOn(pio_get_instance(i), slots, count)) {
      return true;
    }
  }
  return false;
}

#endif
