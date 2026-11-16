// When a session's mapping counts as complete.
//
// Until it is, the board drives no coil from an output frame: a position whose
// mapping frame was lost still has the identity map, and its bit would drive
// whichever coil carries the number equal to the position. EventDispatcher
// itself does not build on the host, so this covers the bookkeeping it uses.

#include <unity.h>

#include "EventDispatcher/MappingProgress.h"

void setUp(void) {}
void tearDown(void) {}

void test_complete_once_every_position_is_named(void) {
  MappingProgress p;
  p.reset(2, 1, 1);
  TEST_ASSERT_FALSE(p.complete());
  TEST_ASSERT_TRUE(p.note(ppuc::v2::kDomainCoil, 0));
  TEST_ASSERT_TRUE(p.note(ppuc::v2::kDomainCoil, 1));
  TEST_ASSERT_TRUE(p.note(ppuc::v2::kDomainLamp, 0));
  TEST_ASSERT_FALSE(p.complete());
  TEST_ASSERT_TRUE(p.note(ppuc::v2::kDomainSwitch, 0));
  TEST_ASSERT_TRUE(p.complete());
}

// The case counting frames got wrong: one lost, another repeated.
void test_a_repeated_frame_does_not_stand_in_for_a_lost_one(void) {
  MappingProgress p;
  p.reset(2, 1, 1);
  p.note(ppuc::v2::kDomainCoil, 0);
  p.note(ppuc::v2::kDomainCoil, 0);
  p.note(ppuc::v2::kDomainLamp, 0);
  p.note(ppuc::v2::kDomainSwitch, 0);
  TEST_ASSERT_FALSE_MESSAGE(p.complete(), "coil position 1 was never named");
  TEST_ASSERT_EQUAL_UINT16(3, p.received());
}

void test_a_position_outside_the_session_is_refused_and_not_counted(void) {
  MappingProgress p;
  p.reset(1, 1, 1);
  TEST_ASSERT_FALSE(p.note(ppuc::v2::kDomainCoil, 1));
  TEST_ASSERT_FALSE(p.note(ppuc::v2::kDomainLamp, 5));
  TEST_ASSERT_FALSE(p.note(99, 0));
  TEST_ASSERT_EQUAL_UINT16(0, p.received());
  TEST_ASSERT_FALSE(p.complete());
}

void test_reset_starts_over(void) {
  MappingProgress p;
  p.reset(1, 0, 0);
  p.note(ppuc::v2::kDomainCoil, 0);
  TEST_ASSERT_TRUE(p.complete());
  p.reset(1, 0, 0);
  TEST_ASSERT_FALSE(p.complete());
}

void test_a_session_without_positions_is_complete_at_once(void) {
  MappingProgress p;
  p.reset(0, 0, 0);
  TEST_ASSERT_TRUE(p.complete());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_complete_once_every_position_is_named);
  RUN_TEST(test_a_repeated_frame_does_not_stand_in_for_a_lost_one);
  RUN_TEST(test_a_position_outside_the_session_is_refused_and_not_counted);
  RUN_TEST(test_reset_starts_over);
  RUN_TEST(test_a_session_without_positions_is_complete_at_once);
  return UNITY_END();
}
