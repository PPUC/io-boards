// Host tests for the lamp matrix of Out_8x10.
//
// The strobe program takes one word per column: bit 0 is GPIO 3, the lowest
// lamp output. What goes into those words decides which lamps light, so it is
// checked here against the board's real pin order - both groups of outputs run
// downwards, and a word built the intuitive way round lights the mirror image
// of the playfield.

#include <unity.h>

#include "IODevices/LampMatrixLogic.h"
#include "PPUCBoardTypes.h"

namespace {

constexpr uint8_t kBasePin = 3;

LampMatrixLogic g_logic;

uint32_t bit(uint8_t gpio) { return 1u << (gpio - kBasePin); }

}  // namespace

void setUp(void) {
  g_logic.configure(ppuc::board::kOut8x10.lampMatrix,
                    ppuc::board::kOut8x10.lampPins, kBasePin);
}
void tearDown(void) {}

void nothing_is_driven_until_a_lamp_is_lit(void) {
  g_logic.registerLamp(0, 11);
  for (uint8_t column = 0; column < 8; column++) {
    TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(column));
  }
}

void a_lamp_drives_its_column_and_its_row(void) {
  // Column 0 is Hi_1 on GPIO 24, row 0 is Lo_1 on GPIO 12.
  g_logic.registerLamp(0, 11);
  TEST_ASSERT_TRUE(g_logic.setLamp(11, true));

  TEST_ASSERT_EQUAL_HEX32(bit(24) | bit(12), g_logic.columnWord(0));
  for (uint8_t column = 1; column < 8; column++) {
    TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(column));
  }

  TEST_ASSERT_TRUE(g_logic.setLamp(11, false));
  TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(0));
}

void the_last_crossing_is_hi_8_and_lo_10(void) {
  g_logic.registerLamp(7 * 10 + 9, 88);
  g_logic.setLamp(88, true);
  TEST_ASSERT_EQUAL_HEX32(bit(17) | bit(3), g_logic.columnWord(7));
}

void lamps_in_one_column_share_its_slot(void) {
  g_logic.registerLamp(3 * 10 + 0, 41);
  g_logic.registerLamp(3 * 10 + 4, 45);
  g_logic.setLamp(41, true);
  g_logic.setLamp(45, true);

  // Column 3 is Hi_4 on GPIO 21; rows 0 and 4 are GPIO 12 and 8.
  TEST_ASSERT_EQUAL_HEX32(bit(21) | bit(12) | bit(8), g_logic.columnWord(3));

  g_logic.setLamp(41, false);
  TEST_ASSERT_EQUAL_HEX32(bit(21) | bit(8), g_logic.columnWord(3));
}

// A row line is shared by every column. It must only be on in the slots of the
// columns whose lamp on that row is lit, or the others ghost.
void a_row_is_only_driven_in_the_slot_of_its_lit_column(void) {
  g_logic.registerLamp(0 * 10 + 2, 13);
  g_logic.registerLamp(1 * 10 + 2, 23);
  g_logic.setLamp(13, true);

  TEST_ASSERT_EQUAL_HEX32(bit(24) | bit(10), g_logic.columnWord(0));
  TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(1));
}

void positions_follow_the_configured_row_count(void) {
  // A WPC lamp matrix is 8 x 8: position 9 is column 1, row 1.
  TEST_ASSERT_TRUE(g_logic.setNumRows(8));
  TEST_ASSERT_TRUE(g_logic.registerLamp(9, 22));
  g_logic.setLamp(22, true);
  TEST_ASSERT_EQUAL_HEX32(bit(23) | bit(11), g_logic.columnWord(1));

  TEST_ASSERT_FALSE(g_logic.registerLamp(64, 1));
  TEST_ASSERT_FALSE(g_logic.setNumRows(11));
  TEST_ASSERT_FALSE(g_logic.setNumRows(0));
}

void an_unknown_lamp_changes_nothing(void) {
  g_logic.registerLamp(0, 11);
  TEST_ASSERT_FALSE(g_logic.setLamp(12, true));
  TEST_ASSERT_FALSE(g_logic.setLamp(0, true));
  TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(0));
}

void a_direct_lamp_is_on_in_every_slot(void) {
  // Lo_10 on GPIO 3, wired between the supply and that one output.
  TEST_ASSERT_TRUE(g_logic.registerDirect(3, 70));
  g_logic.setLamp(70, true);
  for (uint8_t column = 0; column < 8; column++) {
    TEST_ASSERT_EQUAL_HEX32(bit(3), g_logic.columnWord(column));
  }
  g_logic.setLamp(70, false);
  TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(0));
}

void direct_lamps_and_matrix_lamps_combine(void) {
  g_logic.setNumRows(8);
  g_logic.registerLamp(0, 11);    // Hi_1 x Lo_1
  g_logic.registerDirect(3, 70);  // Lo_10, outside the 8 rows in use
  g_logic.setLamp(11, true);
  g_logic.setLamp(70, true);

  TEST_ASSERT_EQUAL_HEX32(bit(24) | bit(12) | bit(3), g_logic.columnWord(0));
  TEST_ASSERT_EQUAL_HEX32(bit(3), g_logic.columnWord(1));
}

// A line of the matrix cannot also carry a lamp of its own: the strobe would
// switch that lamp with the column.
void a_matrix_line_cannot_also_be_a_direct_output(void) {
  g_logic.registerLamp(0, 11);  // uses GPIO 24 and GPIO 12
  TEST_ASSERT_FALSE(g_logic.registerDirect(24, 70));
  TEST_ASSERT_FALSE(g_logic.registerDirect(12, 71));
  TEST_ASSERT_TRUE(g_logic.registerDirect(11, 72));

  // And the other way round.
  TEST_ASSERT_FALSE(g_logic.registerLamp(1, 12));  // row 1 is GPIO 11
}

void only_lamp_outputs_can_be_direct(void) {
  TEST_ASSERT_FALSE(g_logic.registerDirect(13, 70));  // a test point
  TEST_ASSERT_FALSE(g_logic.registerDirect(2, 70));   // RS485 driver enable
  TEST_ASSERT_FALSE(g_logic.registerDirect(25, 70));  // on-board LED
  TEST_ASSERT_FALSE(g_logic.registerDirect(29, 70));  // WS2812
  TEST_ASSERT_FALSE(g_logic.registerDirect(17, 0));
  TEST_ASSERT_TRUE(g_logic.registerDirect(17, 70));
}

void one_number_can_light_several_lamps(void) {
  g_logic.registerLamp(0, 11);
  g_logic.registerLamp(10, 11);
  g_logic.setLamp(11, true);
  TEST_ASSERT_EQUAL_HEX32(bit(24) | bit(12), g_logic.columnWord(0));
  TEST_ASSERT_EQUAL_HEX32(bit(23) | bit(12), g_logic.columnWord(1));
}

void all_off_keeps_the_configuration(void) {
  g_logic.registerLamp(0, 11);
  g_logic.setLamp(11, true);
  g_logic.allOff();
  TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(0));
  TEST_ASSERT_TRUE(g_logic.setLamp(11, true));
  TEST_ASSERT_EQUAL_HEX32(bit(24) | bit(12), g_logic.columnWord(0));
}

void reset_forgets_everything(void) {
  g_logic.setNumRows(8);
  g_logic.registerLamp(0, 11);
  g_logic.registerDirect(3, 70);
  g_logic.setLamp(11, true);
  g_logic.reset();

  TEST_ASSERT_FALSE(g_logic.hasLamps());
  TEST_ASSERT_EQUAL_UINT8(10, g_logic.rows());
  TEST_ASSERT_EQUAL_HEX32(0, g_logic.columnWord(0));
  TEST_ASSERT_FALSE(g_logic.setLamp(11, true));
  // The lines are free again.
  TEST_ASSERT_TRUE(g_logic.registerDirect(24, 70));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(nothing_is_driven_until_a_lamp_is_lit);
  RUN_TEST(a_lamp_drives_its_column_and_its_row);
  RUN_TEST(the_last_crossing_is_hi_8_and_lo_10);
  RUN_TEST(lamps_in_one_column_share_its_slot);
  RUN_TEST(a_row_is_only_driven_in_the_slot_of_its_lit_column);
  RUN_TEST(positions_follow_the_configured_row_count);
  RUN_TEST(an_unknown_lamp_changes_nothing);
  RUN_TEST(a_direct_lamp_is_on_in_every_slot);
  RUN_TEST(direct_lamps_and_matrix_lamps_combine);
  RUN_TEST(a_matrix_line_cannot_also_be_a_direct_output);
  RUN_TEST(only_lamp_outputs_can_be_direct);
  RUN_TEST(one_number_can_light_several_lamps);
  RUN_TEST(all_off_keeps_the_configuration);
  RUN_TEST(reset_forgets_everything);
  return UNITY_END();
}
