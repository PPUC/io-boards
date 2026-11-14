// Which host frames the epoch gate lets through.
//
// The case this file exists for: administration must work outside a session.
// A board starts at epoch 0 and the host at 1, so before the first setup frame
// every version query was dropped, and after a board restarted in a game every
// stats query - the one asking it why.

#include <unity.h>

#include <initializer_list>

#include "EventDispatcher/SessionGate.h"

using namespace ppuc::v2;

void setUp(void) {}
void tearDown(void) {}

void test_administration_works_whatever_the_epoch(void) {
  TEST_ASSERT_TRUE(EpochGate::kProcess ==
                   HostFrameEpochGate(kFrameAdmin, /*host*/ 1, /*board*/ 0));
  TEST_ASSERT_TRUE(EpochGate::kProcess ==
                   HostFrameEpochGate(kFrameAdmin, 41, 37));
  TEST_ASSERT_TRUE(EpochGate::kProcess == HostFrameEpochGate(kFrameAdmin, 5, 5));
}

void test_config_is_used_across_epochs_but_noted(void) {
  TEST_ASSERT_TRUE(EpochGate::kProcessWithGap ==
                   HostFrameEpochGate(kFrameConfig, 1, 0));
  TEST_ASSERT_TRUE(EpochGate::kProcess == HostFrameEpochGate(kFrameConfig, 3, 3));
}

void test_session_frames_from_another_epoch_are_dropped(void) {
  for (FrameType type : {kFrameOutputState, kFrameMapping, kFrameTrigger,
                         kFrameSwitchRefresh}) {
    TEST_ASSERT_TRUE(EpochGate::kDrop == HostFrameEpochGate(type, 2, 1));
    TEST_ASSERT_TRUE(EpochGate::kProcess == HostFrameEpochGate(type, 2, 2));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_administration_works_whatever_the_epoch);
  RUN_TEST(test_config_is_used_across_epochs_but_noted);
  RUN_TEST(test_session_frames_from_another_epoch_are_dropped);
  return UNITY_END();
}
