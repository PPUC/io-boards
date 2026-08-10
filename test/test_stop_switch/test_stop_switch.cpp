// Switches that stop an output, rather than start one.
//
// A fast-flip switch runs a coil while it is closed. This is the other
// polarity: a switch that cuts an output the instant it closes. Two things on a
// machine need it.
//
// A WPC Fliptronic flipper is two windings driven separately, and the
// end-of-stroke contact is what should drop the power winding once the finger
// has arrived - the maximum pulse time is only the net for when that contact
// fails. And a motor-driven assembly, a gun or a cannon, has a switch at each
// end of its travel; missing one drives the assembly into its own stop.
//
// The behaviour that is easy to get wrong, and is most of what is tested here:
//
//   - engaging is on the *closing edge*, not on the switch being closed, or an
//     assembly that starts its travel sitting on an end switch could never
//     move;
//   - releasing is on the level, and a switch-driven output fires again by
//     itself, which is what brings a flipper back up when a heavy ball has
//     pushed the finger down while the button is still held;
//   - the maximum pulse time still wins, and after it cuts an output the switch
//     has to be released before it fires again.

#include <unity.h>

#include "Arduino.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "HardwareStubs.h"
#include "IODevices/PwmDevices.h"

namespace {

constexpr byte kPort = 19;
constexpr byte kCoilNumber = 29;   // a flipper power winding
constexpr byte kPower = 255;
constexpr byte kButton = 112;      // the flipper button, a fast-flip switch
constexpr byte kEos = 200;         // the end-of-stroke contact
constexpr byte kMotorPort = 20;
constexpr byte kMotorNumber = 20;
constexpr byte kEndA = 76;
constexpr byte kEndB = 77;

EventDispatcher* g_dispatcher = nullptr;
PwmDevices* g_pwm = nullptr;

void PowerOn() {
  Event runEvent(EVENT_RUN, 1, 1);
  g_pwm->handleEvent(&runEvent);
}

void SendSwitch(uint16_t number, uint8_t value) {
  stubs::SetSwitchState(number, value != 0);
  Event event(EVENT_SOURCE_SWITCH, number, value);
  g_pwm->handleEvent(&event);
}

void SendSolenoid(byte coilNumber, uint8_t value) {
  Event event(EVENT_SOURCE_SOLENOID, coilNumber, value);
  g_pwm->handleEvent(&event);
}

void Tick(uint32_t ms) {
  stubs::AdvanceMillis(ms);
  g_pwm->update();
}

bool IsOn(byte port) { return stubs::AnalogValue(port) > 0; }

// A flipper power winding: driven by the button, stopped by the EOS, with the
// 40 ms safety timeout the real machine uses.
void RegisterFlipperPower(uint16_t maxPulse = 40) {
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, /*minPT*/ 0, maxPulse,
                          /*hP*/ 0, /*hPAT*/ 0, kButton, kEos, 0);
}

// A gun motor: driven by the host, stopped at either end of its travel.
void RegisterMotor() {
  g_pwm->registerSolenoid(kMotorPort, kMotorNumber, 64, /*minPT*/ 0,
                          /*maxPT*/ 2000, /*hP*/ 0, /*hPAT*/ 0, /*fS*/ 0,
                          kEndA, kEndB, PWM_TYPE_MOTOR);
}

}  // namespace

void setUp(void) {
  stubs::Reset();
  stubs::SetMillis(100'000);
  g_dispatcher = new EventDispatcher();
  g_pwm = new PwmDevices(g_dispatcher);
}

void tearDown(void) {
  delete g_pwm;
  delete g_dispatcher;
  g_pwm = nullptr;
  g_dispatcher = nullptr;
}

// --- the flipper ------------------------------------------------------------

void test_the_eos_cuts_the_power_winding(void) {
  RegisterFlipperPower();
  PowerOn();

  SendSwitch(kButton, 1);
  TEST_ASSERT_TRUE_MESSAGE(IsOn(kPort), "the button should fire the winding");

  SendSwitch(kEos, 1);
  TEST_ASSERT_FALSE_MESSAGE(IsOn(kPort),
                            "the EOS closing must drop the power winding");
}

void test_the_winding_stays_off_while_the_eos_is_closed(void) {
  RegisterFlipperPower();
  PowerOn();

  SendSwitch(kButton, 1);
  SendSwitch(kEos, 1);

  // The player lets go and presses again with the finger still up there.
  SendSwitch(kButton, 0);
  SendSwitch(kButton, 1);

  TEST_ASSERT_FALSE_MESSAGE(IsOn(kPort),
                            "nothing should drive the winding while the EOS is closed");
}

void test_the_winding_fires_again_when_the_eos_opens_under_the_ball(void) {
  // The case this feature exists for. A ball heavy enough to push the finger
  // back down opens the EOS. The hold winding alone cannot lift it, so the
  // power winding has to come back while the button is still held - otherwise
  // the flipper stays down until the player releases and presses again.
  RegisterFlipperPower();
  PowerOn();

  SendSwitch(kButton, 1);
  SendSwitch(kEos, 1);
  TEST_ASSERT_FALSE(IsOn(kPort));

  SendSwitch(kEos, 0);
  Tick(1);

  TEST_ASSERT_TRUE_MESSAGE(IsOn(kPort),
                           "the winding must fire again when the EOS opens with the button held");
}

void test_the_winding_does_not_fire_again_once_the_button_is_released(void) {
  RegisterFlipperPower();
  PowerOn();

  SendSwitch(kButton, 1);
  SendSwitch(kEos, 1);
  SendSwitch(kButton, 0);
  SendSwitch(kEos, 0);
  Tick(1);

  TEST_ASSERT_FALSE_MESSAGE(IsOn(kPort),
                            "no button, no flip");
}

void test_the_max_pulse_time_still_cuts_a_failed_eos(void) {
  // The EOS is unplugged, misadjusted or broken, so it never closes. The
  // timeout is the only thing left, and it is why the timeout exists.
  RegisterFlipperPower(/*maxPulse*/ 40);
  PowerOn();

  SendSwitch(kButton, 1);
  Tick(30);
  TEST_ASSERT_TRUE_MESSAGE(IsOn(kPort), "30 ms is inside the stroke");

  Tick(20);
  TEST_ASSERT_FALSE_MESSAGE(IsOn(kPort),
                            "the winding must be cut once the timeout passes");
}

void test_a_timed_out_winding_waits_for_the_button_before_firing_again(void) {
  // Distinct from the EOS case: a timeout means something is wrong, so it must
  // not re-arm by itself. Only a fresh press may fire it.
  RegisterFlipperPower(/*maxPulse*/ 40);
  PowerOn();

  SendSwitch(kButton, 1);
  Tick(50);
  TEST_ASSERT_FALSE(IsOn(kPort));

  Tick(50);
  TEST_ASSERT_FALSE_MESSAGE(IsOn(kPort),
                            "a timed-out winding must not fire again on its own");

  SendSwitch(kButton, 0);
  SendSwitch(kButton, 1);
  TEST_ASSERT_TRUE_MESSAGE(IsOn(kPort), "a fresh press must fire it");
}

// --- the motor --------------------------------------------------------------

void test_an_end_switch_stops_the_motor(void) {
  RegisterMotor();
  PowerOn();

  SendSolenoid(kMotorNumber, 1);
  TEST_ASSERT_TRUE(IsOn(kMotorPort));

  SendSwitch(kEndB, 1);
  TEST_ASSERT_FALSE_MESSAGE(IsOn(kMotorPort),
                            "reaching the end of its travel must stop the motor");
}

void test_either_end_switch_stops_the_motor(void) {
  RegisterMotor();
  PowerOn();

  SendSolenoid(kMotorNumber, 1);
  SendSwitch(kEndA, 1);

  TEST_ASSERT_FALSE(IsOn(kMotorPort));
}

void test_a_motor_starting_on_an_end_switch_can_still_move(void) {
  // The assembly is parked at one end, which is where it spends most of its
  // life. If a closed end switch blocked the motor outright it could never
  // leave, so engaging is on the closing edge rather than the level.
  RegisterMotor();
  PowerOn();

  stubs::SetSwitchState(kEndA, true);
  Tick(1);

  SendSolenoid(kMotorNumber, 1);
  TEST_ASSERT_TRUE_MESSAGE(IsOn(kMotorPort),
                           "a motor parked on its end switch must still be able to start");
}

void test_a_stopped_motor_does_not_restart_by_itself(void) {
  // Unlike a flipper. The motor stopped because it arrived, not because
  // something went wrong, so the host has to ask again.
  RegisterMotor();
  PowerOn();

  SendSolenoid(kMotorNumber, 1);
  SendSwitch(kEndB, 1);
  TEST_ASSERT_FALSE(IsOn(kMotorPort));

  SendSwitch(kEndB, 0);
  Tick(5);

  TEST_ASSERT_FALSE_MESSAGE(IsOn(kMotorPort),
                            "a motor that reached its end must wait for the host");
}

void test_the_host_can_run_the_motor_again_after_it_left_the_end(void) {
  RegisterMotor();
  PowerOn();

  SendSolenoid(kMotorNumber, 1);
  SendSwitch(kEndB, 1);
  SendSolenoid(kMotorNumber, 0);

  SendSwitch(kEndB, 0);
  Tick(1);
  SendSolenoid(kMotorNumber, 1);

  TEST_ASSERT_TRUE_MESSAGE(IsOn(kMotorPort),
                           "the host must be able to drive it away from the end");
}

void test_the_host_cannot_drive_a_motor_that_is_sitting_on_its_stop(void) {
  RegisterMotor();
  PowerOn();

  SendSolenoid(kMotorNumber, 1);
  SendSwitch(kEndB, 1);
  TEST_ASSERT_FALSE(IsOn(kMotorPort));

  // The ROM keeps asking. The assembly is against its stop, and driving into
  // that is what the switch is there to prevent.
  SendSolenoid(kMotorNumber, 0);
  SendSolenoid(kMotorNumber, 1);

  TEST_ASSERT_FALSE_MESSAGE(IsOn(kMotorPort),
                            "a stop switch must outrank the host");
}

// --- reconciliation ---------------------------------------------------------

void test_a_stop_is_honoured_even_if_the_switch_event_never_arrives(void) {
  // An edge event can be lost. The board reconciles against the switch bitmap
  // every update, so a stop cannot be missed just because a message was.
  RegisterFlipperPower(/*maxPulse*/ 0);
  PowerOn();

  SendSwitch(kButton, 1);
  TEST_ASSERT_TRUE(IsOn(kPort));

  stubs::SetSwitchState(kEos, true);  // no event dispatched
  Tick(1);

  TEST_ASSERT_FALSE_MESSAGE(IsOn(kPort),
                            "a stop switch found closed must cut the output");
}

void test_a_release_is_honoured_even_if_the_switch_event_never_arrives(void) {
  RegisterFlipperPower(/*maxPulse*/ 0);
  PowerOn();

  SendSwitch(kButton, 1);
  SendSwitch(kEos, 1);
  TEST_ASSERT_FALSE(IsOn(kPort));

  stubs::SetSwitchState(kEos, false);  // no event dispatched
  Tick(1);

  TEST_ASSERT_TRUE_MESSAGE(IsOn(kPort),
                           "a stop that was released must not hold the output off forever");
}

// --- outputs with no stop switch --------------------------------------------

void test_an_ordinary_coil_is_unaffected(void) {
  // Everything without a stop switch has to behave exactly as before.
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, 0, 0, 0, kButton);
  PowerOn();

  SendSwitch(kButton, 1);
  TEST_ASSERT_TRUE(IsOn(kPort));

  SendSwitch(kEos, 1);
  TEST_ASSERT_TRUE_MESSAGE(IsOn(kPort),
                           "a switch that is not a stop for this output must not touch it");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_eos_cuts_the_power_winding);
  RUN_TEST(test_the_winding_stays_off_while_the_eos_is_closed);
  RUN_TEST(test_the_winding_fires_again_when_the_eos_opens_under_the_ball);
  RUN_TEST(test_the_winding_does_not_fire_again_once_the_button_is_released);
  RUN_TEST(test_the_max_pulse_time_still_cuts_a_failed_eos);
  RUN_TEST(test_a_timed_out_winding_waits_for_the_button_before_firing_again);
  RUN_TEST(test_an_end_switch_stops_the_motor);
  RUN_TEST(test_either_end_switch_stops_the_motor);
  RUN_TEST(test_a_motor_starting_on_an_end_switch_can_still_move);
  RUN_TEST(test_a_stopped_motor_does_not_restart_by_itself);
  RUN_TEST(test_the_host_can_run_the_motor_again_after_it_left_the_end);
  RUN_TEST(test_the_host_cannot_drive_a_motor_that_is_sitting_on_its_stop);
  RUN_TEST(test_a_stop_is_honoured_even_if_the_switch_event_never_arrives);
  RUN_TEST(test_a_release_is_honoured_even_if_the_switch_event_never_arrives);
  RUN_TEST(test_an_ordinary_coil_is_unaffected);
  return UNITY_END();
}
