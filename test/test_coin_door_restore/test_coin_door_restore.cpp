// What the ROM wants comes back on when the coin door closes.
//
// Opening the door cuts every output. The host keeps sending the ROM's full
// snapshot, but the board acts only on what changes in it, so an output the
// ROM held on through the open door was never asked for again: a trap door
// hold, a magnet, a motor or the start button lamp stayed off after the door
// was closed. Worse, opening the door also made the board forget that game-on
// was set, so every coil stayed dead until the ROM next switched game-on.

#include <unity.h>

#include "Arduino.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "HardwareStubs.h"
#include "IODevices/PwmDevices.h"

namespace {

constexpr byte kHoldPort = 19;  // Trap Door Hold: no maximum pulse time
constexpr byte kHoldNumber = 16;
constexpr byte kKickerPort = 20;  // a 150 ms pulse
constexpr byte kKickerNumber = 1;
constexpr byte kMotorPort = 21;  // runs without game-on
constexpr byte kMotorNumber = 20;
constexpr byte kLampPort = 22;  // Start Button lamp
constexpr byte kLampNumber = 87;
constexpr byte kGameOn = 31;
constexpr uint16_t kCoinDoor = 22;

EventDispatcher* g_dispatcher = nullptr;
PwmDevices* g_pwm = nullptr;

void Configure(byte topic, uint16_t number) {
  ConfigEvent event(/*boardId*/ 1, topic, /*index*/ 0, CONFIG_TOPIC_NUMBER,
                    number);
  g_pwm->handleEvent(&event);
}

void Send(byte source, uint16_t number, bool value) {
  Event event(source, number, value ? 1 : 0);
  g_pwm->handleEvent(&event);
  g_pwm->update();
}

void Door(bool closed) {
  stubs::SetSwitchState(kCoinDoor, closed);
  Send(EVENT_SOURCE_SWITCH, kCoinDoor, closed);
}

void Rom(byte number, bool on) { Send(EVENT_SOURCE_SOLENOID, number, on); }

bool On(byte port) { return stubs::AnalogValue(port) > 0; }

// A game running with the door closed.
void Machine() {
  g_pwm->registerSolenoid(kHoldPort, kHoldNumber, 128, 0, 0, 0, 0, 0);
  g_pwm->registerSolenoid(kKickerPort, kKickerNumber, 255, 0, 150, 0, 0, 0);
  g_pwm->registerSolenoid(kMotorPort, kMotorNumber, 64, 0, 0, 0, 0, 0, 0, 0,
                          PWM_TYPE_MOTOR, /*ignoreGameOn*/ true);
  g_pwm->registerLamp(kLampPort, kLampNumber, 32);
  Send(EVENT_RUN, 1, true);
  Door(true);
  Rom(kGameOn, true);
}

}  // namespace

void setUp(void) {
  stubs::Reset();
  stubs::SetMillis(100'000);
  g_dispatcher = new EventDispatcher();
  g_pwm = new PwmDevices(g_dispatcher);
  Configure(CONFIG_TOPIC_GAME_ON_SOLENOID, kGameOn);
  Configure(CONFIG_TOPIC_COIN_DOOR_CLOSED_SWITCH, kCoinDoor);
}

void tearDown(void) {
  delete g_pwm;
  delete g_dispatcher;
}

void test_a_held_coil_comes_back_when_the_door_closes(void) {
  Machine();
  Rom(kHoldNumber, true);
  TEST_ASSERT_TRUE(On(kHoldPort));
  Door(false);
  TEST_ASSERT_FALSE(On(kHoldPort));
  Door(true);
  TEST_ASSERT_TRUE_MESSAGE(On(kHoldPort), "the ROM never stopped holding it");
}

void test_a_coil_turned_on_while_the_door_was_open_comes_on(void) {
  Machine();
  Door(false);
  Rom(kHoldNumber, true);
  TEST_ASSERT_FALSE(On(kHoldPort));
  Door(true);
  TEST_ASSERT_TRUE(On(kHoldPort));
}

void test_a_coil_the_rom_turned_off_stays_off(void) {
  Machine();
  Rom(kHoldNumber, true);
  Door(false);
  Rom(kHoldNumber, false);
  Door(true);
  TEST_ASSERT_FALSE(On(kHoldPort));
}

void test_a_finished_pulse_is_not_repeated(void) {
  Machine();
  Rom(kKickerNumber, true);
  Rom(kKickerNumber, false);
  Door(false);
  Door(true);
  TEST_ASSERT_FALSE(On(kKickerPort));
}

void test_a_restored_pulse_is_still_bounded(void) {
  Machine();
  Door(false);
  Rom(kKickerNumber, true);  // and the ROM does not let go
  Door(true);
  TEST_ASSERT_TRUE(On(kKickerPort));
  stubs::AdvanceMillis(151);
  g_pwm->update();
  TEST_ASSERT_FALSE(On(kKickerPort));
}

void test_the_lamp_comes_back(void) {
  Machine();
  Send(EVENT_SOURCE_LIGHT, kLampNumber, true);
  TEST_ASSERT_TRUE(On(kLampPort));
  Door(false);
  TEST_ASSERT_FALSE(On(kLampPort));
  Door(true);
  TEST_ASSERT_TRUE(On(kLampPort));
}

void test_without_game_on_only_exempt_outputs_come_back(void) {
  Machine();
  Rom(kMotorNumber, true);
  Rom(kHoldNumber, true);
  Rom(kGameOn, false);
  Door(false);
  Door(true);
  TEST_ASSERT_TRUE_MESSAGE(On(kMotorPort), "it runs without a game");
  TEST_ASSERT_FALSE_MESSAGE(On(kHoldPort), "game-on still gates it");
}

void test_the_rom_is_obeyed_again_right_after_the_door_closes(void) {
  // Game-on does not change when the door opens or closes. The board used to
  // forget it was set, and refused every coil until the next ball.
  Machine();
  Door(false);
  Door(true);
  Rom(kKickerNumber, true);
  TEST_ASSERT_TRUE(On(kKickerPort));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_rom_is_obeyed_again_right_after_the_door_closes);
  RUN_TEST(test_a_held_coil_comes_back_when_the_door_closes);
  RUN_TEST(test_a_coil_turned_on_while_the_door_was_open_comes_on);
  RUN_TEST(test_a_coil_the_rom_turned_off_stays_off);
  RUN_TEST(test_a_finished_pulse_is_not_repeated);
  RUN_TEST(test_a_restored_pulse_is_still_bounded);
  RUN_TEST(test_the_lamp_comes_back);
  RUN_TEST(test_without_game_on_only_exempt_outputs_come_back);
  return UNITY_END();
}
