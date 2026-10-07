// Host tests for the board profile table.
//
// PPUCBoardTypes.h is the one place that says which GPIO does what on each
// board, and both the firmware and the host act on it: the firmware refuses a
// device on a pin the board does not have, the host refuses to send one. A
// wrong entry does not fail loudly - it drives the wrong output - so the
// entries are pinned here against what the schematics say.

#include <unity.h>

#include "PPUCBoardTypes.h"

using namespace ppuc::board;

namespace {

uint8_t countBits(uint32_t mask) {
  uint8_t n = 0;
  for (; mask != 0; mask &= mask - 1) n++;
  return n;
}

const uint8_t kAllTypes[] = {
    ppuc::v2::kBoardTypeIo16_8_1, ppuc::v2::kBoardTypeIo16x8Matrix,
    ppuc::v2::kBoardTypeOut8x10, ppuc::v2::kBoardTypeOpto16};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void every_protocol_board_type_has_a_profile(void) {
  for (uint8_t type : kAllTypes) {
    TEST_ASSERT_EQUAL_UINT8(type, profileFor(type).type);
  }
  TEST_ASSERT_EQUAL_UINT8(ppuc::v2::kBoardTypeUnknown, profileFor(0x7F).type);
}

void an_unknown_board_allows_nothing(void) {
  const Profile unknown = profileFor(0x7F);
  for (uint8_t pin = 0; pin < 32; pin++) {
    TEST_ASSERT_FALSE(unknown.allowsSwitch(pin));
    TEST_ASSERT_FALSE(unknown.allowsPwm(pin));
    TEST_ASSERT_FALSE(unknown.allowsDirectLamp(pin));
    TEST_ASSERT_FALSE(unknown.allowsLedString(pin));
  }
}

// GPIO 0-2 are the RS485 UART and its driver enable, 25 the on-board LED and
// 28 the address ladder. Nothing may ever be configured onto them.
void no_board_hands_out_a_reserved_pin(void) {
  const uint32_t reserved = pinRange(0, 2) | pinBit(25) | pinBit(28);
  for (uint8_t type : kAllTypes) {
    const Profile p = profileFor(type);
    TEST_ASSERT_EQUAL_HEX32(0, p.inputPins & reserved);
    TEST_ASSERT_EQUAL_HEX32(0, p.pwmPins & reserved);
    TEST_ASSERT_EQUAL_HEX32(0, p.lampPins & reserved);
    TEST_ASSERT_EQUAL_HEX32(0, p.safeOffPins & reserved);
    TEST_ASSERT_EQUAL_UINT8(29, p.ledPin);
    TEST_ASSERT_TRUE(p.allowsLedString(29));
    TEST_ASSERT_FALSE(p.allowsSwitch(29));
    TEST_ASSERT_FALSE(p.allowsPwm(29));
  }
}

void io_16_8_1_has_sixteen_inputs_and_eight_outputs(void) {
  const Profile p = kIo16_8_1;
  TEST_ASSERT_EQUAL_UINT8(16, countBits(p.inputPins));
  for (uint8_t pin = 3; pin <= 18; pin++) {
    TEST_ASSERT_TRUE(p.allowsSwitch(pin));
    // The inputs double as low-power outputs.
    TEST_ASSERT_TRUE(p.allowsPwm(pin));
  }
  const uint8_t outputs[] = {19, 20, 21, 22, 23, 24, 26, 27};
  for (uint8_t pin : outputs) {
    TEST_ASSERT_TRUE(p.allowsPwm(pin));
    TEST_ASSERT_FALSE(p.allowsSwitch(pin));
  }
  // The 4-column matrix on the inputs stays exactly as it was.
  TEST_ASSERT_TRUE(p.has(kCapSwitchMatrix));
  TEST_ASSERT_FALSE(p.has(kCapStrobedSwitchMatrix));
  TEST_ASSERT_EQUAL_UINT8(4, p.matrix.columns);
  TEST_ASSERT_EQUAL_UINT8(8, p.matrix.maxRows);
  TEST_ASSERT_EQUAL_UINT8(15, p.matrix.columnsBasePin);
}

// The watchdog used to loop over GPIO 19 to 26: it switched the on-board LED
// off and left the eighth high-power output, on GPIO 27, alone.
void the_watchdog_covers_all_eight_high_power_outputs(void) {
  const uint8_t outputs[] = {19, 20, 21, 22, 23, 24, 26, 27};
  uint32_t expected = 0;
  for (uint8_t pin : outputs) expected |= pinBit(pin);
  TEST_ASSERT_EQUAL_HEX32(expected, kIo16_8_1.safeOffPins);
}

void opto_16_has_inputs_and_an_led_string_only(void) {
  const Profile p = kOpto16;
  TEST_ASSERT_EQUAL_UINT8(16, countBits(p.inputPins));
  TEST_ASSERT_FALSE(p.has(kCapPwmOutputs));
  TEST_ASSERT_FALSE(p.has(kCapSwitchMatrix));
  TEST_ASSERT_FALSE(p.has(kCapStrobedSwitchMatrix));
  TEST_ASSERT_FALSE(p.has(kCapLampMatrix));
  for (uint8_t pin = 0; pin < 32; pin++) {
    TEST_ASSERT_FALSE(p.allowsPwm(pin));
    TEST_ASSERT_FALSE(p.allowsDirectLamp(pin));
  }
  TEST_ASSERT_EQUAL_HEX32(0, p.safeOffPins);
}

// Out_1 is GPIO 27 descending to Out_8 on GPIO 19, skipping the LED on 25. The
// opposite order to IO_16_8_1.
void io_16x8_matrix_strobes_run_downwards_around_the_led(void) {
  const Profile p = kIo16x8Matrix;
  const uint8_t expected[] = {27, 26, 24, 23, 22, 21, 20, 19};
  TEST_ASSERT_EQUAL_UINT8(8, p.strobedMatrix.strobes);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, p.strobedMatrix.strobePins, 8);
  TEST_ASSERT_EQUAL_UINT8(19, p.strobedMatrix.strobeBasePin);
  TEST_ASSERT_EQUAL_UINT8(9, p.strobedMatrix.strobeSpan);
  TEST_ASSERT_EQUAL_UINT8(3, p.strobedMatrix.returnsBasePin);
  TEST_ASSERT_EQUAL_UINT8(16, p.strobedMatrix.maxReturns);
  TEST_ASSERT_FALSE(p.isStrobePin(25));
  for (uint8_t pin : expected) {
    TEST_ASSERT_TRUE(p.isStrobePin(pin));
    TEST_ASSERT_TRUE(p.allowsPwm(pin));
  }
  // Its matrix is the strobed one, not IO_16_8_1's scan on the inputs.
  TEST_ASSERT_FALSE(p.has(kCapSwitchMatrix));
  // Signal outputs only: the inputs are not outputs here.
  for (uint8_t pin = 3; pin <= 18; pin++) {
    TEST_ASSERT_TRUE(p.allowsSwitch(pin));
    TEST_ASSERT_FALSE(p.allowsPwm(pin));
  }
}

void out_8x10_is_lamp_drivers_only(void) {
  const Profile p = kOut8x10;
  const uint8_t columns[] = {24, 23, 22, 21, 20, 19, 18, 17};
  const uint8_t rows[] = {12, 11, 10, 9, 8, 7, 6, 5, 4, 3};
  TEST_ASSERT_EQUAL_UINT8(8, p.lampMatrix.columns);
  TEST_ASSERT_EQUAL_UINT8(10, p.lampMatrix.rows);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(columns, p.lampMatrix.columnPins, 8);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rows, p.lampMatrix.rowPins, 10);

  uint32_t lines = 0;
  for (uint8_t pin : columns) lines |= pinBit(pin);
  for (uint8_t pin : rows) lines |= pinBit(pin);
  TEST_ASSERT_EQUAL_UINT8(18, countBits(lines));
  TEST_ASSERT_EQUAL_HEX32(lines, p.lampPins);
  // Ten of these switch 3 A. Every one of them is the watchdog's business.
  TEST_ASSERT_EQUAL_HEX32(lines, p.safeOffPins);

  TEST_ASSERT_FALSE(p.has(kCapPwmOutputs));
  TEST_ASSERT_FALSE(p.has(kCapDedicatedSwitches));
  for (uint8_t pin = 0; pin < 32; pin++) {
    TEST_ASSERT_FALSE(p.allowsPwm(pin));
    TEST_ASSERT_FALSE(p.allowsSwitch(pin));
  }
  // The test points between the two groups are not outputs.
  for (uint8_t pin = 13; pin <= 16; pin++) {
    TEST_ASSERT_FALSE(p.allowsDirectLamp(pin));
  }
}

// The reason Out_8x10 has no PWM capability: six low-side outputs share their
// PWM channel with a high-side one.
void gpios_sixteen_apart_share_a_pwm_channel(void) {
  TEST_ASSERT_TRUE(sharesPwmChannel(3, 19));
  TEST_ASSERT_TRUE(sharesPwmChannel(19, 3));
  TEST_ASSERT_TRUE(sharesPwmChannel(8, 24));
  TEST_ASSERT_TRUE(sharesPwmChannel(10, 26));
  TEST_ASSERT_FALSE(sharesPwmChannel(19, 19));
  TEST_ASSERT_FALSE(sharesPwmChannel(19, 20));
  TEST_ASSERT_FALSE(sharesPwmChannel(4, 19));

  uint8_t shared = 0;
  for (uint8_t row = 0; row < kOut8x10.lampMatrix.rows; row++) {
    for (uint8_t column = 0; column < kOut8x10.lampMatrix.columns; column++) {
      if (sharesPwmChannel(kOut8x10.lampMatrix.rowPins[row],
                           kOut8x10.lampMatrix.columnPins[column])) {
        shared++;
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT8(6, shared);
}

void pin_range_is_inclusive(void) {
  TEST_ASSERT_EQUAL_HEX32(0x00000008u, pinRange(3, 3));
  TEST_ASSERT_EQUAL_HEX32(0x0007FFF8u, pinRange(3, 18));
  TEST_ASSERT_EQUAL_HEX32(0x80000000u, pinRange(31, 31));
  TEST_ASSERT_EQUAL_HEX32(0u, pinBit(32));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(every_protocol_board_type_has_a_profile);
  RUN_TEST(an_unknown_board_allows_nothing);
  RUN_TEST(no_board_hands_out_a_reserved_pin);
  RUN_TEST(io_16_8_1_has_sixteen_inputs_and_eight_outputs);
  RUN_TEST(the_watchdog_covers_all_eight_high_power_outputs);
  RUN_TEST(opto_16_has_inputs_and_an_led_string_only);
  RUN_TEST(io_16x8_matrix_strobes_run_downwards_around_the_led);
  RUN_TEST(out_8x10_is_lamp_drivers_only);
  RUN_TEST(gpios_sixteen_apart_share_a_pwm_channel);
  RUN_TEST(pin_range_is_inclusive);
  return UNITY_END();
}
