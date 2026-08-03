// Host tests for dynamic PIO allocation.
//
// The firmware behaviour these protect cannot be observed without hardware,
// so the properties worth pinning are the ones a board would fail at silently:
// a device whose two state machines end up on different blocks (the columns
// would lose their pins), and a failed claim that leaves fragments of itself
// behind (the next device gets a machine with less than it should have).

#include <unity.h>

#include "IODevices/PioAllocation.h"

namespace {

// Lengths are all this file needs from a program.
constexpr uint16_t kNoInstructions[1] = {0};

pio_program_t makeProgram(uint8_t length) {
  pio_program_t p;
  p.instructions = kNoInstructions;
  p.length = length;
  p.origin = -1;
  return p;
}

const pio_program_t kSmall = makeProgram(4);
const pio_program_t kHuge = makeProgram(PIO_STUB_INSTRUCTION_SLOTS);

}  // namespace

void setUp(void) { pio_stub::reset(); }
void tearDown(void) {}

void claims_a_single_slot(void) {
  PioSlot slot;
  slot.program = &kSmall;

  TEST_ASSERT_TRUE(pioClaimSlots(&slot, 1));
  TEST_ASSERT_TRUE(slot.claimed);
  TEST_ASSERT_NOT_NULL(slot.pio);
  TEST_ASSERT_EQUAL_UINT(1, pio_stub::totalClaimedStateMachines());
  TEST_ASSERT_EQUAL_UINT(kSmall.length, pio_stub::totalInstructionsUsed());
}

void keeps_both_slots_on_one_block(void) {
  PioSlot slots[2];
  slots[0].program = &kSmall;
  slots[1].program = &kSmall;

  TEST_ASSERT_TRUE(pioClaimSlots(slots, 2));
  // The whole point: a GPIO can only be routed to one PIO block, so a device
  // driving and observing the same pins must not be split across blocks.
  TEST_ASSERT_EQUAL_PTR(slots[0].pio, slots[1].pio);
  TEST_ASSERT_TRUE(slots[0].sm != slots[1].sm);
}

void moves_to_the_second_block_rather_than_splitting(void) {
  // Leave only one free state machine on block 0.
  PIO first = pio_get_instance(0);
  for (int i = 0; i < PIO_STUB_NUM_SMS - 1; i++) {
    pio_claim_unused_sm(first, false);
  }

  PioSlot slots[2];
  slots[0].program = &kSmall;
  slots[1].program = &kSmall;

  TEST_ASSERT_TRUE(pioClaimSlots(slots, 2));
  TEST_ASSERT_EQUAL_PTR(slots[0].pio, slots[1].pio);
  // Block 0 could have satisfied one of them. Taking it would have split the
  // pair, so both must have gone to block 1.
  TEST_ASSERT_EQUAL_PTR(pio_get_instance(1), slots[0].pio);
  // ...and block 0 must not have been left holding a stray program.
  TEST_ASSERT_EQUAL_UINT(0, pio_stub::instructionsUsed(0));
}

void fails_when_no_state_machines_remain(void) {
  for (uint block = 0; block < NUM_PIOS; block++) {
    for (int i = 0; i < PIO_STUB_NUM_SMS; i++) {
      pio_claim_unused_sm(pio_get_instance(block), false);
    }
  }

  PioSlot slot;
  slot.program = &kSmall;

  TEST_ASSERT_FALSE(pioClaimSlots(&slot, 1));
  TEST_ASSERT_FALSE(slot.claimed);
  TEST_ASSERT_NULL(slot.pio);
}

void leaves_no_program_behind_when_the_claim_fails(void) {
  // Every state machine taken, but instruction space wide open: the allocator
  // will add the program, discover there is no state machine for it, and has
  // to hand the instruction space back.
  for (uint block = 0; block < NUM_PIOS; block++) {
    for (int i = 0; i < PIO_STUB_NUM_SMS; i++) {
      pio_claim_unused_sm(pio_get_instance(block), false);
    }
  }

  PioSlot slot;
  slot.program = &kSmall;
  TEST_ASSERT_FALSE(pioClaimSlots(&slot, 1));

  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalInstructionsUsed());
}

void rolls_back_a_partial_two_slot_claim(void) {
  // Enough room for the first program on either block but not the second.
  // The first must not be left loaded.
  PioSlot slots[2];
  slots[0].program = &kSmall;
  slots[1].program = &kHuge;

  TEST_ASSERT_FALSE(pioClaimSlots(slots, 2));
  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalInstructionsUsed());
  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalClaimedStateMachines());
  TEST_ASSERT_FALSE(slots[0].claimed);
  TEST_ASSERT_FALSE(slots[1].claimed);
}

void release_gives_the_resources_back(void) {
  PioSlot slots[2];
  slots[0].program = &kSmall;
  slots[1].program = &kSmall;
  TEST_ASSERT_TRUE(pioClaimSlots(slots, 2));

  pioReleaseSlots(slots, 2);

  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalClaimedStateMachines());
  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalInstructionsUsed());
  TEST_ASSERT_FALSE(slots[0].claimed);
  TEST_ASSERT_NULL(slots[0].pio);
}

void reconfiguration_does_not_leak_instruction_space(void) {
  // A matrix that changes polarity or row count releases and re-claims. Doing
  // that repeatedly must not consume more of the block each time - the old
  // code added the new programs without removing the old ones.
  for (int i = 0; i < 5; i++) {
    PioSlot slots[2];
    slots[0].program = &kSmall;
    slots[1].program = &kSmall;
    TEST_ASSERT_TRUE(pioClaimSlots(slots, 2));
    TEST_ASSERT_EQUAL_UINT(2 * kSmall.length, pio_stub::totalInstructionsUsed());
    pioReleaseSlots(slots, 2);
    TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalInstructionsUsed());
  }
}

void releasing_unclaimed_slots_is_harmless(void) {
  PioSlot slots[2];
  slots[0].program = &kSmall;
  slots[1].program = &kSmall;

  pioReleaseSlots(slots, 2);

  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalClaimedStateMachines());
  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalInstructionsUsed());
}

void an_unconfigured_device_costs_nothing(void) {
  // The reason for the whole change: the matrix used to reserve two state
  // machines on every board whether or not one was configured.
  TEST_ASSERT_EQUAL_UINT(0, pio_stub::totalClaimedStateMachines());

  PioSlot slot;
  slot.program = &kSmall;
  TEST_ASSERT_TRUE(pioClaimSlots(&slot, 1));

  // A lone switch reader leaves the other seven machines for everyone else.
  TEST_ASSERT_EQUAL_UINT(1, pio_stub::totalClaimedStateMachines());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(claims_a_single_slot);
  RUN_TEST(keeps_both_slots_on_one_block);
  RUN_TEST(moves_to_the_second_block_rather_than_splitting);
  RUN_TEST(fails_when_no_state_machines_remain);
  RUN_TEST(leaves_no_program_behind_when_the_claim_fails);
  RUN_TEST(rolls_back_a_partial_two_slot_claim);
  RUN_TEST(release_gives_the_resources_back);
  RUN_TEST(reconfiguration_does_not_leak_instruction_space);
  RUN_TEST(releasing_unclaimed_slots_is_harmless);
  RUN_TEST(an_unconfigured_device_costs_nothing);
  return UNITY_END();
}
