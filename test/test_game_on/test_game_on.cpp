// Game-on gate tests for PwmDevices.
//
// While the game-on solenoid is off, coil commands are discarded. A ROM may
// still have to drive an output then: Dirty Harry searches for its gun at boot
// and waits for the result. Such an output is exempt from this one gate, and
// only from this one - the coin door and stopping the board still cut it.

#include <unity.h>

#include "Arduino.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "HardwareStubs.h"
#include "IODevices/PwmDevices.h"

namespace {

constexpr byte kMotorPort = 19;
constexpr byte kMotorNumber = 20;
constexpr byte kCoilPort = 20;
constexpr byte kCoilNumber = 9;
constexpr byte kMotorPower = 64;
constexpr byte kCoilPower = 255;
constexpr byte kGameOn = 31;
constexpr uint16_t kCoinDoor = 22;

EventDispatcher* g_dispatcher = nullptr;
PwmDevices* g_pwm = nullptr;

void Configure(byte topic, uint16_t number) {
  ConfigEvent event(/*boardId*/ 1, topic, /*index*/ 0, CONFIG_TOPIC_NUMBER, number);
  g_pwm->handleEvent(&event);
}

void Run(bool running) {
  Event event(EVENT_RUN, 1, running ? 1 : 0);
  g_pwm->handleEvent(&event);
}

void Door(bool closed) {
  stubs::SetSwitchState(kCoinDoor, closed);
  Event event(EVENT_SOURCE_SWITCH, kCoinDoor, closed ? 1 : 0);
  g_pwm->handleEvent(&event);
}

void Solenoid(byte number, bool on) {
  Event event(EVENT_SOURCE_SOLENOID, number, on ? 1 : 0);
  g_pwm->handleEvent(&event);
}

bool MotorIsOn() { return stubs::AnalogValue(kMotorPort) > 0; }
bool CoilIsOn() { return stubs::AnalogValue(kCoilPort) > 0; }

void RegisterMachine(bool ignoreGameOn) {
  g_pwm->registerSolenoid(kMotorPort, kMotorNumber, kMotorPower, 0, 0, 0, 0, 0,
                          0, 0, PWM_TYPE_MOTOR, ignoreGameOn);
  g_pwm->registerSolenoid(kCoilPort, kCoilNumber, kCoilPower, 0, 150, 0, 0, 0);
}

// Booted, door closed, no game running.
void Attract(bool ignoreGameOn) {
  RegisterMachine(ignoreGameOn);
  Run(true);
  Door(true);
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

void test_motor_is_gated_by_default(void) {
  Attract(false);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_FALSE(MotorIsOn());
}

void test_exempt_motor_runs_with_no_game(void) {
  Attract(true);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_EQUAL(kMotorPower, stubs::AnalogValue(kMotorPort));
  Solenoid(kMotorNumber, false);
  TEST_ASSERT_FALSE(MotorIsOn());
}

void test_exemption_does_not_open_the_gate_for_other_coils(void) {
  Attract(true);
  Solenoid(kCoilNumber, true);
  TEST_ASSERT_FALSE(CoilIsOn());
}

void test_game_ending_leaves_exempt_motor_running(void) {
  Attract(true);
  Solenoid(kGameOn, true);
  Solenoid(kMotorNumber, true);
  Solenoid(kCoilNumber, true);
  TEST_ASSERT_TRUE(MotorIsOn());
  TEST_ASSERT_TRUE(CoilIsOn());

  Solenoid(kGameOn, false);
  TEST_ASSERT_TRUE(MotorIsOn());
  TEST_ASSERT_FALSE(CoilIsOn());
}

void test_coin_door_still_cuts_exempt_motor(void) {
  Attract(true);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_TRUE(MotorIsOn());

  Door(false);
  TEST_ASSERT_FALSE(MotorIsOn());

  Solenoid(kMotorNumber, false);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_FALSE(MotorIsOn());
}

void test_stopping_the_board_cuts_exempt_motor(void) {
  Attract(true);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_TRUE(MotorIsOn());

  Run(false);
  TEST_ASSERT_FALSE(MotorIsOn());

  Solenoid(kMotorNumber, false);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_FALSE(MotorIsOn());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_motor_is_gated_by_default);
  RUN_TEST(test_exempt_motor_runs_with_no_game);
  RUN_TEST(test_exemption_does_not_open_the_gate_for_other_coils);
  RUN_TEST(test_game_ending_leaves_exempt_motor_running);
  RUN_TEST(test_coin_door_still_cuts_exempt_motor);
  RUN_TEST(test_stopping_the_board_cuts_exempt_motor);
  return UNITY_END();
}
