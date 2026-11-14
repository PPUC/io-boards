// Why a board last started, as it reports it in the stats.
//
// A board that restarts in a game loses its configuration and the host ends
// the game. Whether the board's own watchdog rebooted it - its main loop hung -
// or its supply dropped out decides where to look next, and on Dirty Harry the
// log could not tell.

#include <string.h>

#include <initializer_list>
#include <unity.h>

#include "ResetCause.h"

using namespace ppuc::v2;

void setUp(void) {}
void tearDown(void) {}

void test_a_power_on_is_a_power_on(void) {
  TEST_ASSERT_EQUAL(kResetCausePowerOn,
                    ResetCauseFrom(false, 0, true, false, false));
}

void test_the_reset_pin_is_the_reset_pin(void) {
  TEST_ASSERT_EQUAL(kResetCauseRunPin,
                    ResetCauseFrom(false, 0, false, true, false));
}

void test_a_debugger_restart_is_named(void) {
  TEST_ASSERT_EQUAL(kResetCauseDebugger,
                    ResetCauseFrom(false, 0, true, false, true));
}

void test_a_marked_watchdog_reboot_says_why(void) {
  for (uint8_t cause : {kResetCauseMainLoopStalled, kResetCauseResetFrame,
                        kResetCauseFirmwareUpdate}) {
    // The chip reset bits still hold the power-on from long before: a
    // watchdog reboot is not a chip-level reset and does not clear them.
    TEST_ASSERT_EQUAL(cause,
                      ResetCauseFrom(true, RebootMarker(cause), true, false,
                                     false));
  }
}

void test_an_unmarked_watchdog_reboot_is_not_taken_for_a_power_on(void) {
  TEST_ASSERT_EQUAL(kResetCauseWatchdogOther,
                    ResetCauseFrom(true, 0, true, false, false));
}

void test_a_foreign_or_garbled_marker_is_not_trusted(void) {
  TEST_ASSERT_EQUAL(kResetCauseWatchdogOther,
                    ResetCauseFrom(true, 0x12345603u, true, false, false));
  TEST_ASSERT_EQUAL(kResetCauseWatchdogOther,
                    ResetCauseFrom(true, RebootMarker(kResetCausePowerOn),
                                   true, false, false));
}

void test_a_marker_left_from_before_a_power_cut_is_ignored(void) {
  // Scratch registers lose their contents without power, but if one did
  // survive, the watchdog reason says the marker is not about this boot.
  TEST_ASSERT_EQUAL(
      kResetCausePowerOn,
      ResetCauseFrom(false, RebootMarker(kResetCauseMainLoopStalled), true,
                     false, false));
}

void test_the_cause_travels_in_the_top_byte_of_the_gate_word(void) {
  const uint32_t gate = 0x00160F0Bu;  // door switch 22, game-on 15, flags
  const uint32_t word =
      gate | (static_cast<uint32_t>(kResetCauseMainLoopStalled)
              << kStatsResetCauseShift);
  TEST_ASSERT_EQUAL(kResetCauseMainLoopStalled, StatsResetCause(word));
  TEST_ASSERT_EQUAL_HEX32(gate, word & 0x00FFFFFFu);
  TEST_ASSERT_EQUAL(kResetCauseNotReported, StatsResetCause(gate));
}

void test_every_cause_has_a_name(void) {
  for (uint8_t cause = kResetCausePowerOn; cause <= kResetCauseDebugger;
       cause++) {
    TEST_ASSERT_NOT_EQUAL(0, strcmp(ResetCauseName(cause), "not reported"));
  }
  TEST_ASSERT_EQUAL_STRING("not reported",
                           ResetCauseName(kResetCauseNotReported));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_power_on_is_a_power_on);
  RUN_TEST(test_the_reset_pin_is_the_reset_pin);
  RUN_TEST(test_a_debugger_restart_is_named);
  RUN_TEST(test_a_marked_watchdog_reboot_says_why);
  RUN_TEST(test_an_unmarked_watchdog_reboot_is_not_taken_for_a_power_on);
  RUN_TEST(test_a_foreign_or_garbled_marker_is_not_trusted);
  RUN_TEST(test_a_marker_left_from_before_a_power_cut_is_ignored);
  RUN_TEST(test_the_cause_travels_in_the_top_byte_of_the_gate_word);
  RUN_TEST(test_every_cause_has_a_name);
  return UNITY_END();
}
