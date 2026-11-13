// Board-controlled flippers.
//
// A flipper winding marked CONFIG_TOPIC_BOARD_FLIPPER follows its button
// whenever the host has turned the flippers on, which it does when a game
// starts and undoes when the game ends. The ROM's flipper pulses and the
// game-on solenoid do not reach it: on Dirty Harry those were what dropped a
// held flipper or raised it late. The coin door, tilt and stopping the board
// still cut it, and the EOS and the maximum pulse time still bound the power
// winding.

#include <unity.h>

#include "Arduino.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "HardwareStubs.h"
#include "IODevices/PwmDevices.h"

namespace {

// Dirty Harry's lower left flipper, and the sling on the same board.
constexpr byte kPowerPort = 19;
constexpr byte kPowerNumber = 47;
constexpr byte kHoldPort = 20;
constexpr byte kHoldNumber = 48;
constexpr byte kButton = 114;
constexpr byte kEos = 113;
constexpr byte kSlingPort = 22;
constexpr byte kSlingNumber = 9;
constexpr byte kSlingSwitch = 61;
constexpr byte kGameOn = 31;
constexpr uint16_t kCoinDoor = 22;
constexpr uint16_t kTilt = 14;

EventDispatcher* g_dispatcher = nullptr;
PwmDevices* g_pwm = nullptr;

void Configure(byte topic, uint16_t number) {
  ConfigEvent event(/*boardId*/ 1, topic, /*index*/ 0, CONFIG_TOPIC_NUMBER,
                    number);
  g_pwm->handleEvent(&event);
}

// An event, then a pass of the main loop, which runs PwmDevices::update()
// between any two of them.
void Send(byte source, uint16_t number, bool value) {
  Event event(source, number, value ? 1 : 0);
  g_pwm->handleEvent(&event);
  g_pwm->update();
}

void Switch(uint16_t number, bool closed) {
  stubs::SetSwitchState(number, closed);
  Send(EVENT_SOURCE_SWITCH, number, closed);
}

void Enable(bool on) { Send(EVENT_SOURCE_FLIPPER_ENABLE, 0, on); }
void Solenoid(byte number, bool on) { Send(EVENT_SOURCE_SOLENOID, number, on); }

void Advance(uint32_t ms) {
  // In steps, as the main loop would.
  for (uint32_t t = 0; t < ms; t++) {
    stubs::AdvanceMillis(1);
    g_pwm->update();
  }
}

bool PowerOn() { return stubs::AnalogValue(kPowerPort) > 0; }
bool HoldOn() { return stubs::AnalogValue(kHoldPort) > 0; }
bool SlingOn() { return stubs::AnalogValue(kSlingPort) > 0; }

// Booted, door closed, no game running, flippers in board mode.
void Machine(bool boardFlippers = true) {
  g_pwm->registerSolenoid(kPowerPort, kPowerNumber, 255, 0, 40, 0, 0, kButton,
                          kEos, 0, PWM_TYPE_SOLENOID, false, boardFlippers);
  g_pwm->registerSolenoid(kHoldPort, kHoldNumber, 255, 0, 0, 0, 0, kButton, 0,
                          0, PWM_TYPE_SOLENOID, false, boardFlippers);
  g_pwm->registerSolenoid(kSlingPort, kSlingNumber, 192, 0, 150, 0, 0,
                          kSlingSwitch);
  Send(EVENT_RUN, 1, true);
  Switch(kCoinDoor, true);
}

}  // namespace

void setUp(void) {
  stubs::Reset();
  stubs::SetMillis(100'000);
  g_dispatcher = new EventDispatcher();
  g_pwm = new PwmDevices(g_dispatcher);
  Configure(CONFIG_TOPIC_GAME_ON_SOLENOID, kGameOn);
  Configure(CONFIG_TOPIC_COIN_DOOR_CLOSED_SWITCH, kCoinDoor);
  Configure(CONFIG_TOPIC_TILT_SWITCH, kTilt);
}

void tearDown(void) {
  delete g_pwm;
  delete g_dispatcher;
}

void test_flippers_stay_down_until_the_host_turns_them_on(void) {
  Machine();
  // Game-on alone is not enough any more.
  Solenoid(kGameOn, true);
  Switch(kButton, true);
  TEST_ASSERT_FALSE(PowerOn());
  TEST_ASSERT_FALSE(HoldOn());
}

void test_an_enabled_flipper_follows_its_button_without_game_on(void) {
  Machine();
  Enable(true);
  Switch(kButton, true);
  TEST_ASSERT_TRUE(PowerOn());
  TEST_ASSERT_TRUE(HoldOn());
  Switch(kButton, false);
  TEST_ASSERT_FALSE(PowerOn());
  TEST_ASSERT_FALSE(HoldOn());
}

void test_the_roms_flipper_pulses_are_ignored(void) {
  Machine();
  Solenoid(kGameOn, true);
  Enable(true);

  // Its own pulse with no button: nothing.
  Solenoid(kHoldNumber, true);
  Solenoid(kPowerNumber, true);
  TEST_ASSERT_FALSE(PowerOn());
  TEST_ASSERT_FALSE(HoldOn());
  Solenoid(kPowerNumber, false);
  Solenoid(kHoldNumber, false);

  // Its "off" while the player holds the button: the finger stays up.
  Switch(kButton, true);
  Solenoid(kHoldNumber, true);
  Solenoid(kHoldNumber, false);
  Advance(500);
  TEST_ASSERT_TRUE(HoldOn());
}

void test_game_on_dropping_does_not_drop_a_held_flipper(void) {
  Machine();
  Solenoid(kGameOn, true);
  Enable(true);
  Switch(kButton, true);
  Solenoid(kGameOn, false);
  Advance(100);
  TEST_ASSERT_TRUE(HoldOn());
  // And the button still works between balls.
  Switch(kButton, false);
  Switch(kButton, true);
  TEST_ASSERT_TRUE(PowerOn());
  TEST_ASSERT_TRUE(HoldOn());
}

void test_other_coils_keep_the_game_on_gate(void) {
  Machine();
  Enable(true);
  Switch(kSlingSwitch, true);
  TEST_ASSERT_FALSE_MESSAGE(SlingOn(), "a sling is not a board flipper");
  Switch(kSlingSwitch, false);
  Advance(200);
  Solenoid(kGameOn, true);
  Switch(kSlingSwitch, true);
  TEST_ASSERT_TRUE(SlingOn());
}

void test_game_over_drops_a_held_flipper_and_needs_a_new_press(void) {
  Machine();
  Enable(true);
  Switch(kButton, true);
  Enable(false);
  TEST_ASSERT_FALSE(PowerOn());
  TEST_ASSERT_FALSE(HoldOn());

  // A new game with the button still held: no flip until it is pressed again.
  Enable(true);
  Advance(10);
  TEST_ASSERT_FALSE(HoldOn());
  Switch(kButton, false);
  Switch(kButton, true);
  TEST_ASSERT_TRUE(HoldOn());
}

void test_the_coin_door_cuts_board_flippers(void) {
  Machine();
  Enable(true);
  Switch(kButton, true);
  Switch(kCoinDoor, false);
  TEST_ASSERT_FALSE(PowerOn());
  TEST_ASSERT_FALSE(HoldOn());
  Switch(kButton, false);
  Switch(kButton, true);
  TEST_ASSERT_FALSE(HoldOn());
}

void test_tilt_cuts_board_flippers(void) {
  Machine();
  Enable(true);
  Switch(kButton, true);
  Switch(kTilt, true);
  TEST_ASSERT_FALSE(HoldOn());
  Switch(kButton, false);
  Switch(kButton, true);
  TEST_ASSERT_FALSE(HoldOn());
}

void test_stopping_the_board_cuts_board_flippers(void) {
  Machine();
  Enable(true);
  Switch(kButton, true);
  Send(EVENT_RUN, 1, false);
  TEST_ASSERT_FALSE(HoldOn());
}

void test_eos_and_max_pulse_still_bound_the_power_winding(void) {
  Machine();
  Enable(true);
  Switch(kButton, true);
  Switch(kEos, true);
  TEST_ASSERT_FALSE(PowerOn());
  TEST_ASSERT_TRUE(HoldOn());
  Switch(kEos, false);
  Switch(kButton, false);

  // With an EOS that never closes, the 40 ms cap ends it.
  Switch(kButton, true);
  TEST_ASSERT_TRUE(PowerOn());
  Advance(45);
  TEST_ASSERT_FALSE(PowerOn());
  TEST_ASSERT_TRUE(HoldOn());
}

void test_reset_turns_the_flippers_off(void) {
  Machine();
  Enable(true);
  g_pwm->reset();
  Machine();
  Switch(kButton, true);
  TEST_ASSERT_FALSE(HoldOn());
}

void test_without_board_mode_flippers_follow_game_on_as_before(void) {
  Machine(/*boardFlippers*/ false);
  Enable(true);
  Switch(kButton, true);
  TEST_ASSERT_FALSE_MESSAGE(HoldOn(), "the enable is for board flippers only");
  Switch(kButton, false);
  Solenoid(kGameOn, true);
  Switch(kButton, true);
  TEST_ASSERT_TRUE(HoldOn());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_flippers_stay_down_until_the_host_turns_them_on);
  RUN_TEST(test_an_enabled_flipper_follows_its_button_without_game_on);
  RUN_TEST(test_the_roms_flipper_pulses_are_ignored);
  RUN_TEST(test_game_on_dropping_does_not_drop_a_held_flipper);
  RUN_TEST(test_other_coils_keep_the_game_on_gate);
  RUN_TEST(test_game_over_drops_a_held_flipper_and_needs_a_new_press);
  RUN_TEST(test_the_coin_door_cuts_board_flippers);
  RUN_TEST(test_tilt_cuts_board_flippers);
  RUN_TEST(test_stopping_the_board_cuts_board_flippers);
  RUN_TEST(test_eos_and_max_pulse_still_bound_the_power_winding);
  RUN_TEST(test_reset_turns_the_flippers_off);
  RUN_TEST(test_without_board_mode_flippers_follow_game_on_as_before);
  return UNITY_END();
}
