// The switch state the outputs read, kept by switch number.
//
// The case this file exists for: a held flipper button must still read closed
// while a session resync is under way. The dense bitmap it used to come from
// is addressed through the session's mapping, which a resync puts back to the
// identity map until the mapping frames are in again. On Dirty Harry, 55
// switch positions and flipper buttons numbered 112 to 116, the button
// resolved to no position and read open, and the flipper dropped.
//
// EventDispatcher itself does not build on the host, so what is covered here
// is the store: nothing about it depends on positions, and only clear() - a
// restart or a reset - forgets a switch.

#include <unity.h>

#include "EventDispatcher/SwitchNumberStates.h"

void setUp(void) {}
void tearDown(void) {}

void test_a_closed_switch_reads_closed_whatever_its_number(void) {
  SwitchNumberStates states;
  // Above any switch position count Dirty Harry has, and the coin door.
  states.record(112, true);
  states.record(200, true);
  TEST_ASSERT_TRUE(states.get(112));
  TEST_ASSERT_TRUE(states.get(200));
}

void test_switches_do_not_share_state(void) {
  SwitchNumberStates states;
  states.record(112, true);
  // Neighbours in the same byte, and the position an identity map would have
  // confused it with on a 55-switch game.
  TEST_ASSERT_FALSE(states.get(111));
  TEST_ASSERT_FALSE(states.get(113));
  TEST_ASSERT_FALSE(states.get(112 - 55));
  states.record(113, true);
  states.record(112, false);
  TEST_ASSERT_FALSE(states.get(112));
  TEST_ASSERT_TRUE(states.get(113));
}

void test_a_switch_never_reported_reads_open(void) {
  SwitchNumberStates states;
  for (uint16_t n = 0; n < SwitchNumberStates::kNumbers; n++) {
    TEST_ASSERT_FALSE(states.get(n));
  }
}

void test_every_byte_sized_number_is_covered(void) {
  // PwmDevices and HighPowerOffAware hold switch numbers in a byte, so every
  // number they can ask about must be answered from here.
  for (uint16_t n = 0; n <= 255; n++) {
    TEST_ASSERT_TRUE(SwitchNumberStates::covers(n));
    SwitchNumberStates states;
    states.record(n, true);
    TEST_ASSERT_TRUE(states.get(n));
  }
}

void test_a_higher_number_is_left_to_the_bitmap(void) {
  SwitchNumberStates states;
  TEST_ASSERT_FALSE(SwitchNumberStates::covers(256));
  states.record(256, true);  // must not write out of bounds
  TEST_ASSERT_FALSE(states.get(256));
  TEST_ASSERT_FALSE(states.get(0));
}

void test_only_clear_forgets_a_switch(void) {
  SwitchNumberStates states;
  states.record(114, true);
  // Recording other switches, as a resync's re-read does, leaves it alone.
  for (uint16_t n = 0; n < 100; n++) {
    states.record(n, false);
  }
  TEST_ASSERT_TRUE(states.get(114));
  states.clear();
  TEST_ASSERT_FALSE(states.get(114));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_closed_switch_reads_closed_whatever_its_number);
  RUN_TEST(test_switches_do_not_share_state);
  RUN_TEST(test_a_switch_never_reported_reads_open);
  RUN_TEST(test_every_byte_sized_number_is_covered);
  RUN_TEST(test_a_higher_number_is_left_to_the_bitmap);
  RUN_TEST(test_only_clear_forgets_a_switch);
  return UNITY_END();
}
