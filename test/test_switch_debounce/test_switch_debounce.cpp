// When a waiting switch edge may be accepted.
//
// The case this file exists for: a fast-flip switch must not take the first
// bounce after a press for a release. It did, because an opening only had to
// come a debounce window after the previous accepted opening, and a flipper
// button's bounce lands microseconds after the press.

#include <unity.h>

#include "IODevices/SwitchDebounce.h"

namespace {

constexpr uint32_t kWindow = 10000;  // 10 ms, the configured default
constexpr uint32_t kNow = 5000000;

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_a_fast_flip_bounce_right_after_a_press_is_not_a_release(void) {
  // Pressed 1 ms ago, released for 200 us now; the last accepted release was
  // long ago, which used to be all that was asked.
  const bool due = SwitchEdgeDue(SWITCH_DEBOUNCE_FAST_FLIP, /*closing*/ false,
                                 kNow, /*pendingSince*/ kNow - 200,
                                 /*lastAccepted*/ kNow - 2000000, kWindow);
  TEST_ASSERT_FALSE_MESSAGE(due, "a bounce must not release the flipper");
}

void test_a_fast_flip_release_counts_once_it_has_held(void) {
  TEST_ASSERT_TRUE(SwitchEdgeDue(SWITCH_DEBOUNCE_FAST_FLIP, false, kNow,
                                 kNow - kWindow, kNow - 2000000, kWindow));
  TEST_ASSERT_FALSE(SwitchEdgeDue(SWITCH_DEBOUNCE_FAST_FLIP, false, kNow,
                                  kNow - kWindow + 1, kNow - 2000000, kWindow));
}

void test_a_fast_flip_release_without_a_previous_one_still_has_to_hold(void) {
  TEST_ASSERT_FALSE(SwitchEdgeDue(SWITCH_DEBOUNCE_FAST_FLIP, false, kNow,
                                  kNow - 100, /*never*/ 0, kWindow));
}

void test_a_deferred_fast_flip_press_goes_once_the_last_one_is_a_window_old(
    void) {
  // A press that came too soon after the previous accepted press.
  TEST_ASSERT_FALSE(SwitchEdgeDue(SWITCH_DEBOUNCE_FAST_FLIP, true, kNow,
                                  kNow - 100, kNow - 5000, kWindow));
  TEST_ASSERT_TRUE(SwitchEdgeDue(SWITCH_DEBOUNCE_FAST_FLIP, true, kNow,
                                 kNow - 100, kNow - kWindow, kWindow));
}

void test_a_standard_switch_needs_the_full_window_either_way(void) {
  for (int closing = 0; closing <= 1; ++closing) {
    TEST_ASSERT_FALSE(SwitchEdgeDue(SWITCH_DEBOUNCE_STANDARD, closing != 0,
                                    kNow, kNow - 9999, 0, kWindow));
    TEST_ASSERT_TRUE(SwitchEdgeDue(SWITCH_DEBOUNCE_STANDARD, closing != 0, kNow,
                                   kNow - kWindow, 0, kWindow));
  }
}

void test_the_clock_wrapping_does_not_stall_an_edge(void) {
  // micros() wraps after about 71 minutes.
  const uint32_t since = 0xFFFFFFFFu - 4000;
  const uint32_t now = 6000;  // 10001 us later
  TEST_ASSERT_TRUE(
      SwitchEdgeDue(SWITCH_DEBOUNCE_FAST_FLIP, false, now, since, 0, kWindow));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_fast_flip_bounce_right_after_a_press_is_not_a_release);
  RUN_TEST(test_a_fast_flip_release_counts_once_it_has_held);
  RUN_TEST(test_a_fast_flip_release_without_a_previous_one_still_has_to_hold);
  RUN_TEST(
      test_a_deferred_fast_flip_press_goes_once_the_last_one_is_a_window_old);
  RUN_TEST(test_a_standard_switch_needs_the_full_window_either_way);
  RUN_TEST(test_the_clock_wrapping_does_not_stall_an_edge);
  return UNITY_END();
}
