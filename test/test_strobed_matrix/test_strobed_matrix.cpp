// Host tests for the strobed switch matrix of IO_16x8_matrix.
//
// The scan program pushes one word per strobe: the strobe pattern in the low
// nine bits, the sixteen returns above. Everything that turns those words into
// switch numbers is here, where it can be checked without a board - which
// column a pattern means on a board whose strobes run downwards around the
// on-board LED, which position a return is, and when a change is real.

#include <unity.h>

#include "IODevices/StrobedSwitchMatrixLogic.h"
#include "PPUCBoardTypes.h"

namespace {

using Logic = StrobedSwitchMatrixLogic;

Logic g_logic;
uint64_t g_now = 0;

// The word the scan pushes for one column, with the given returns high.
uint32_t sample(uint8_t strobeGpio, uint16_t returns) {
  return (static_cast<uint32_t>(returns) << 9) | (1u << (strobeGpio - 19));
}

// One full scan later.
uint8_t scan(uint8_t strobeGpio, uint16_t returns, Logic::Edge* edges,
             uint8_t maxEdges = 16) {
  g_now += 1000;
  return g_logic.decode(sample(strobeGpio, returns), g_now, edges, maxEdges);
}

}  // namespace

void setUp(void) {
  g_logic.configure(ppuc::board::kIo16x8Matrix.strobedMatrix);
  g_now = 1'000'000;
}
void tearDown(void) {}

void a_strobe_pattern_selects_its_connector_column(void) {
  // Out_1 is GPIO 27, Out_8 is GPIO 19.
  TEST_ASSERT_EQUAL_UINT8(0, g_logic.columnOf(1u << (27 - 19)));
  TEST_ASSERT_EQUAL_UINT8(1, g_logic.columnOf(1u << (26 - 19)));
  TEST_ASSERT_EQUAL_UINT8(2, g_logic.columnOf(1u << (24 - 19)));
  TEST_ASSERT_EQUAL_UINT8(7, g_logic.columnOf(1u << 0));
}

void the_led_slot_and_garbage_select_nothing(void) {
  // GPIO 25, in the middle of the strobes, is the on-board LED.
  TEST_ASSERT_EQUAL_UINT8(Logic::kNoColumn, g_logic.columnOf(1u << (25 - 19)));
  TEST_ASSERT_EQUAL_UINT8(Logic::kNoColumn, g_logic.columnOf(0));
  TEST_ASSERT_EQUAL_UINT8(Logic::kNoColumn, g_logic.columnOf(0b11));

  g_logic.registerSwitch(0, 11);
  Logic::Edge edges[16];
  TEST_ASSERT_EQUAL_UINT8(0, scan(25, 0xFFFF, edges));
  TEST_ASSERT_EQUAL_UINT8(0, scan(25, 0xFFFF, edges));
}

void a_switch_is_reported_once_two_scans_agree(void) {
  // Column 2 (Out_3, GPIO 24), row 5 (In_6), sixteen rows per column.
  g_logic.registerSwitch(2 * 16 + 5, 42);
  Logic::Edge edges[16];

  TEST_ASSERT_EQUAL_UINT8(0, scan(24, 0, edges));
  TEST_ASSERT_EQUAL_UINT8(0, scan(24, 1u << 5, edges));  // first sighting
  TEST_ASSERT_EQUAL_UINT8(1, scan(24, 1u << 5, edges));  // confirmed
  TEST_ASSERT_EQUAL_UINT8(42, edges[0].number);
  TEST_ASSERT_EQUAL_UINT8(1, edges[0].state);

  // No repeat while it stays closed.
  TEST_ASSERT_EQUAL_UINT8(0, scan(24, 1u << 5, edges));
}

void a_single_scan_glitch_is_not_reported(void) {
  g_logic.registerSwitch(5, 42);
  Logic::Edge edges[16];

  TEST_ASSERT_EQUAL_UINT8(0, scan(27, 0, edges));
  TEST_ASSERT_EQUAL_UINT8(0, scan(27, 1u << 5, edges));
  TEST_ASSERT_EQUAL_UINT8(0, scan(27, 0, edges));
  TEST_ASSERT_EQUAL_UINT8(0, scan(27, 0, edges));
}

void the_same_return_in_another_column_is_another_switch(void) {
  g_logic.registerSwitch(0 * 16 + 3, 10);
  g_logic.registerSwitch(7 * 16 + 3, 80);
  Logic::Edge edges[16];

  scan(19, 0, edges);
  scan(19, 1u << 3, edges);
  TEST_ASSERT_EQUAL_UINT8(1, scan(19, 1u << 3, edges));
  TEST_ASSERT_EQUAL_UINT8(80, edges[0].number);

  // Column 0 never saw it.
  scan(27, 0, edges);
  TEST_ASSERT_EQUAL_UINT8(0, scan(27, 0, edges));
}

void positions_follow_the_configured_row_count(void) {
  // A WPC matrix is 8 x 8: position 9 is column 1, row 1.
  TEST_ASSERT_TRUE(g_logic.setNumRows(8));
  g_logic.registerSwitch(9, 22);
  Logic::Edge edges[16];

  scan(26, 0, edges);
  scan(26, 1u << 1, edges);
  TEST_ASSERT_EQUAL_UINT8(1, scan(26, 1u << 1, edges));
  TEST_ASSERT_EQUAL_UINT8(22, edges[0].number);

  // Returns beyond the row count are not part of the matrix.
  scan(26, (1u << 1) | (1u << 12), edges);
  TEST_ASSERT_EQUAL_UINT8(0, scan(26, (1u << 1) | (1u << 12), edges));
}

void positions_outside_the_matrix_are_refused(void) {
  TEST_ASSERT_TRUE(g_logic.registerSwitch(127, 1));
  TEST_ASSERT_FALSE(g_logic.registerSwitch(128, 1));
  TEST_ASSERT_FALSE(g_logic.registerSwitch(0, 0));

  TEST_ASSERT_FALSE(g_logic.setNumRows(0));
  TEST_ASSERT_FALSE(g_logic.setNumRows(17));
  TEST_ASSERT_TRUE(g_logic.setNumRows(8));
  TEST_ASSERT_TRUE(g_logic.registerSwitch(63, 1));
  TEST_ASSERT_FALSE(g_logic.registerSwitch(64, 1));
}

void active_low_returns_read_closed_as_zero(void) {
  g_logic.setActiveLow(true);
  g_logic.registerSwitch(0, 11);
  Logic::Edge edges[16];

  // Everything high: every switch open.
  scan(27, 0xFFFF, edges);
  TEST_ASSERT_EQUAL_UINT8(0, scan(27, 0xFFFF, edges));

  scan(27, 0xFFFE, edges);
  TEST_ASSERT_EQUAL_UINT8(1, scan(27, 0xFFFE, edges));
  TEST_ASSERT_EQUAL_UINT8(11, edges[0].number);
  TEST_ASSERT_EQUAL_UINT8(1, edges[0].state);
}

void a_switch_closed_at_power_on_is_reported(void) {
  g_logic.registerSwitch(0, 11);
  Logic::Edge edges[16];

  TEST_ASSERT_EQUAL_UINT8(0, scan(27, 1, edges));
  TEST_ASSERT_EQUAL_UINT8(1, scan(27, 1, edges));
  TEST_ASSERT_EQUAL_UINT8(1, edges[0].state);
}

void chatter_inside_the_lockout_is_held_back(void) {
  g_logic.registerSwitch(0, 11);
  Logic::Edge edges[16];
  uint64_t now = 5'000'000;

  // Closes, confirmed over two samples 300 us apart.
  g_logic.decode(sample(27, 0), now, edges, 16);
  g_logic.decode(sample(27, 1), now += 300, edges, 16);
  TEST_ASSERT_EQUAL_UINT8(1,
                          g_logic.decode(sample(27, 1), now += 300, edges, 16));

  // Opens and closes again within the 2 ms lockout of the first close.
  g_logic.decode(sample(27, 0), now += 300, edges, 16);
  TEST_ASSERT_EQUAL_UINT8(1,
                          g_logic.decode(sample(27, 0), now += 300, edges, 16));
  TEST_ASSERT_EQUAL_UINT8(0, edges[0].state);
  g_logic.decode(sample(27, 1), now += 300, edges, 16);
  TEST_ASSERT_EQUAL_UINT8(0,
                          g_logic.decode(sample(27, 1), now += 300, edges, 16));

  // Still closed after the lockout: now it counts.
  now += STROBED_MATRIX_DEBOUNCE_US;
  TEST_ASSERT_EQUAL_UINT8(1, g_logic.decode(sample(27, 1), now, edges, 16));
  TEST_ASSERT_EQUAL_UINT8(1, edges[0].state);
}

void an_edge_that_does_not_fit_is_reported_on_the_next_scan(void) {
  g_logic.registerSwitch(0, 11);
  g_logic.registerSwitch(1, 12);
  Logic::Edge edges[16];

  scan(27, 0b11, edges, 1);
  TEST_ASSERT_EQUAL_UINT8(1, scan(27, 0b11, edges, 1));
  TEST_ASSERT_EQUAL_UINT8(11, edges[0].number);
  TEST_ASSERT_EQUAL_UINT8(1, scan(27, 0b11, edges, 1));
  TEST_ASSERT_EQUAL_UINT8(12, edges[0].number);
}

void a_snapshot_reports_every_registered_switch(void) {
  g_logic.registerSwitch(0, 11);
  g_logic.registerSwitch(16, 21);
  Logic::Edge edges[16];
  Logic::Edge states[128];

  // Before any scan: all open.
  TEST_ASSERT_EQUAL_UINT8(2, g_logic.snapshot(states, 128));
  TEST_ASSERT_EQUAL_UINT8(0, states[0].state);
  TEST_ASSERT_EQUAL_UINT8(0, states[1].state);

  scan(26, 1, edges);
  scan(26, 1, edges);
  TEST_ASSERT_EQUAL_UINT8(2, g_logic.snapshot(states, 128));
  TEST_ASSERT_EQUAL_UINT8(11, states[0].number);
  TEST_ASSERT_EQUAL_UINT8(0, states[0].state);
  TEST_ASSERT_EQUAL_UINT8(21, states[1].number);
  TEST_ASSERT_EQUAL_UINT8(1, states[1].state);
}

void reset_forgets_the_configuration(void) {
  g_logic.setNumRows(8);
  g_logic.setActiveLow(true);
  g_logic.registerSwitch(0, 11);
  g_logic.reset();

  TEST_ASSERT_FALSE(g_logic.hasSwitches());
  TEST_ASSERT_EQUAL_UINT8(16, g_logic.rows());
  Logic::Edge states[128];
  TEST_ASSERT_EQUAL_UINT8(0, g_logic.snapshot(states, 128));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(a_strobe_pattern_selects_its_connector_column);
  RUN_TEST(the_led_slot_and_garbage_select_nothing);
  RUN_TEST(a_switch_is_reported_once_two_scans_agree);
  RUN_TEST(a_single_scan_glitch_is_not_reported);
  RUN_TEST(the_same_return_in_another_column_is_another_switch);
  RUN_TEST(positions_follow_the_configured_row_count);
  RUN_TEST(positions_outside_the_matrix_are_refused);
  RUN_TEST(active_low_returns_read_closed_as_zero);
  RUN_TEST(a_switch_closed_at_power_on_is_reported);
  RUN_TEST(chatter_inside_the_lockout_is_held_back);
  RUN_TEST(an_edge_that_does_not_fit_is_reported_on_the_next_scan);
  RUN_TEST(a_snapshot_reports_every_registered_switch);
  RUN_TEST(reset_forgets_the_configuration);
  return UNITY_END();
}
