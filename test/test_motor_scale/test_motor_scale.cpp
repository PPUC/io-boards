// Motor power scale tests for PwmDevices.
//
// A ROM sets a motor's speed by pulsing its driver. The host measures that and
// sends it as a scale of the configured power. Without one the motor runs at
// plain power, and the coil bit alone decides whether it runs at all.

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
constexpr byte kPower = 64;

EventDispatcher* g_dispatcher = nullptr;
PwmDevices* g_pwm = nullptr;

void Run() {
  Event event(EVENT_RUN, 1, 1);
  g_pwm->handleEvent(&event);
}

void Solenoid(byte number, bool on) {
  Event event(EVENT_SOURCE_SOLENOID, number, on ? 1 : 0);
  g_pwm->handleEvent(&event);
}

void Scale(byte number, byte scale) {
  Event event(EVENT_SOURCE_POWER_SCALE, number, scale);
  g_pwm->handleEvent(&event);
}

void Tick(uint32_t ms) {
  stubs::AdvanceMillis(ms);
  g_pwm->update();
}

int MotorPower() { return stubs::AnalogValue(kMotorPort); }

void RegisterMotor() {
  g_pwm->registerSolenoid(kMotorPort, kMotorNumber, kPower, 0, 0, 0, 0, 0, 0, 0,
                          PWM_TYPE_MOTOR);
}

}  // namespace

void setUp(void) {
  stubs::Reset();
  stubs::SetMillis(100'000);
  g_dispatcher = new EventDispatcher();
  g_pwm = new PwmDevices(g_dispatcher);
  RegisterMotor();
  Run();
}

void tearDown(void) {
  delete g_pwm;
  delete g_dispatcher;
  g_pwm = nullptr;
  g_dispatcher = nullptr;
}

void test_without_a_scale_a_motor_runs_at_its_power(void) {
  Solenoid(kMotorNumber, true);
  Tick(MOTOR_START_KICK_MS + 10);
  TEST_ASSERT_EQUAL(kPower, MotorPower());
}

void test_scale_applies_after_the_start_kick(void) {
  Scale(kMotorNumber, 128);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_EQUAL(kPower, MotorPower());

  Tick(MOTOR_START_KICK_MS - 1);
  TEST_ASSERT_EQUAL(kPower, MotorPower());

  Tick(1);
  TEST_ASSERT_EQUAL(32, MotorPower());
}

void test_a_scale_changes_a_running_motor(void) {
  Solenoid(kMotorNumber, true);
  Tick(MOTOR_START_KICK_MS);
  Scale(kMotorNumber, 128);
  Tick(1);
  TEST_ASSERT_EQUAL(32, MotorPower());

  Scale(kMotorNumber, 255);
  Tick(1);
  TEST_ASSERT_EQUAL(kPower, MotorPower());
}

void test_a_scale_does_not_start_a_stopped_motor(void) {
  Scale(kMotorNumber, 128);
  Tick(MOTOR_START_KICK_MS);
  TEST_ASSERT_EQUAL(0, MotorPower());
}

void test_the_coil_bit_still_stops_it(void) {
  Scale(kMotorNumber, 128);
  Solenoid(kMotorNumber, true);
  Tick(MOTOR_START_KICK_MS);
  Solenoid(kMotorNumber, false);
  TEST_ASSERT_EQUAL(0, MotorPower());
  Tick(1);
  TEST_ASSERT_EQUAL(0, MotorPower());
}

void test_each_activation_kicks_again(void) {
  Scale(kMotorNumber, 128);
  Solenoid(kMotorNumber, true);
  Tick(MOTOR_START_KICK_MS);
  Solenoid(kMotorNumber, false);
  Tick(1);
  Solenoid(kMotorNumber, true);
  TEST_ASSERT_EQUAL(kPower, MotorPower());
}

void test_a_tiny_scale_does_not_round_to_nothing(void) {
  Scale(kMotorNumber, 1);
  Solenoid(kMotorNumber, true);
  Tick(MOTOR_START_KICK_MS);
  TEST_ASSERT_EQUAL(1, MotorPower());
}

void test_zero_means_no_measurement(void) {
  Scale(kMotorNumber, 128);
  Scale(kMotorNumber, 0);
  Solenoid(kMotorNumber, true);
  Tick(MOTOR_START_KICK_MS);
  TEST_ASSERT_EQUAL(kPower, MotorPower());
}

void test_a_coil_is_never_scaled(void) {
  g_pwm->registerSolenoid(kCoilPort, kCoilNumber, 255, 0, 150, 0, 0, 0);
  Scale(kCoilNumber, 128);
  Solenoid(kCoilNumber, true);
  Tick(MOTOR_START_KICK_MS);
  TEST_ASSERT_EQUAL(255, stubs::AnalogValue(kCoilPort));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_without_a_scale_a_motor_runs_at_its_power);
  RUN_TEST(test_scale_applies_after_the_start_kick);
  RUN_TEST(test_a_scale_changes_a_running_motor);
  RUN_TEST(test_a_scale_does_not_start_a_stopped_motor);
  RUN_TEST(test_the_coil_bit_still_stops_it);
  RUN_TEST(test_each_activation_kicks_again);
  RUN_TEST(test_a_tiny_scale_does_not_round_to_nothing);
  RUN_TEST(test_zero_means_no_measurement);
  RUN_TEST(test_a_coil_is_never_scaled);
  return UNITY_END();
}
