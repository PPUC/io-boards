// Pulse-envelope tests for PwmDevices.
//
// This is the firmware's hardware-safety layer, and the reason it is the first
// thing covered: a stuck fast-flip switch on a kicker or jet bumper can refire
// a coil repeatedly and overheat it badly enough to risk damage. The
// protection has to be board-local so it holds even when the host is busy,
// delayed or gone.
//
// The invariant under test, stated generally so it also covers magnets and
// motors when they arrive:
//
//   No configured solenoid may be energised beyond its declared protection,
//   regardless of host behaviour.
//
// Everything here runs natively against the shims in test/stubs/. Time is
// driven by the test, so pulse boundaries are asserted exactly.

#include <unity.h>

#include "Arduino.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "HardwareStubs.h"
#include "IODevices/PwmDevices.h"

namespace {

constexpr byte kPort = 19;        // a high-power output
constexpr byte kCoilNumber = 7;
constexpr byte kPower = 255;
constexpr uint16_t kSwitch = 33;  // fast-flip switch for the fast tests

EventDispatcher* g_dispatcher = nullptr;
PwmDevices* g_pwm = nullptr;

// Brings the board to the state it is in during a game: high power on and the
// coin door closed. Without EVENT_RUN, HighPowerOffAware gates every output
// off and no coil can fire at all.
void PowerOn() {
  Event runEvent(EVENT_RUN, 1, 1);
  g_pwm->handleEvent(&runEvent);
}

void SendSolenoid(uint8_t value) {
  Event event(EVENT_SOURCE_SOLENOID, kCoilNumber, value);
  g_pwm->handleEvent(&event);
}

void SendSwitch(uint16_t number, uint8_t value) {
  stubs::SetSwitchState(number, value != 0);
  Event event(EVENT_SOURCE_SWITCH, number, value);
  g_pwm->handleEvent(&event);
}

// Advances the clock and pumps update(), which is what the firmware main loop
// does. Pulse expiry is evaluated in update(), not on the event.
void Tick(uint32_t ms) {
  stubs::AdvanceMillis(ms);
  g_pwm->update();
}

bool CoilIsOn() { return stubs::AnalogValue(kPort) > 0; }

}  // namespace

void setUp(void) {
  stubs::Reset();
  stubs::SetMillis(100'000);  // away from 0, which several fields use as unset
  g_dispatcher = new EventDispatcher();
  g_pwm = new PwmDevices(g_dispatcher);
}

void tearDown(void) {
  delete g_pwm;
  delete g_dispatcher;
  g_pwm = nullptr;
  g_dispatcher = nullptr;
}

// --- host-driven pulses -----------------------------------------------------

void test_host_on_energises_the_coil(void) {
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, 0, 0, 0, 0);
  PowerOn();

  SendSolenoid(1);

  TEST_ASSERT_TRUE(CoilIsOn());
  TEST_ASSERT_EQUAL_INT(kPower, stubs::AnalogValue(kPort));
}

void test_host_off_before_min_pulse_defers_the_release(void) {
  // The host asking for "off" inside the protected window must not cut the
  // pulse short; the coil stays on until the minimum has elapsed.
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, /*minPT*/ 50,
                          /*maxPT*/ 0, 0, 0, 0);
  PowerOn();

  SendSolenoid(1);
  TEST_ASSERT_TRUE(CoilIsOn());

  SendSolenoid(0);
  Tick(10);
  TEST_ASSERT_TRUE_MESSAGE(CoilIsOn(),
                           "release inside minPulseTime must be deferred");

  Tick(60);  // now past the 50 ms minimum
  TEST_ASSERT_FALSE_MESSAGE(CoilIsOn(),
                            "coil must release once minPulseTime has elapsed");
}

void test_max_pulse_time_forces_the_coil_off(void) {
  // The host never sends "off" - a hung host, a stalled bus, a lost frame. The
  // board must cut the coil regardless. This is the property that stops a
  // solenoid burning when the host dies mid-pulse.
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, /*minPT*/ 0,
                          /*maxPT*/ 100, 0, 0, 0);
  PowerOn();

  SendSolenoid(1);
  TEST_ASSERT_TRUE(CoilIsOn());

  Tick(50);
  TEST_ASSERT_TRUE(CoilIsOn());

  Tick(60);  // 110 ms total, past the 100 ms maximum
  TEST_ASSERT_FALSE_MESSAGE(
      CoilIsOn(), "maxPulseTime must force the coil off without any host event");
}

void test_hold_power_reduces_duty_after_activation(void) {
  // A coil that must stay engaged indefinitely drops to a current it can
  // dissipate. This is one of the three valid protection mechanisms, and the
  // reason maxPulseTime 0 is legitimate for such a coil.
  constexpr byte kHoldPower = 40;
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, /*maxPT*/ 0,
                          kHoldPower, /*hPAT*/ 30, 0);
  PowerOn();

  SendSolenoid(1);
  TEST_ASSERT_EQUAL_INT(kPower, stubs::AnalogValue(kPort));

  Tick(40);  // past the hold-power activation time
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      kHoldPower, stubs::AnalogValue(kPort),
      "power must drop to holdPower after holdPowerActivationTime");
  TEST_ASSERT_TRUE_MESSAGE(CoilIsOn(), "the coil must remain engaged");
}

// --- fast-switch driven pulses ----------------------------------------------

void test_fast_switch_fires_the_coil_locally(void) {
  // No host involvement at all: the board reacts to its own switch.
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, /*maxPT*/ 100, 0, 0,
                          /*fastSwitch*/ kSwitch);
  PowerOn();

  SendSwitch(kSwitch, 1);

  TEST_ASSERT_TRUE_MESSAGE(CoilIsOn(),
                           "a fast switch must fire its coil board-locally");
}

void test_fast_switch_release_inside_min_pulse_is_deferred(void) {
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, /*minPT*/ 50,
                          /*maxPT*/ 200, 0, 0, kSwitch);
  PowerOn();

  SendSwitch(kSwitch, 1);
  TEST_ASSERT_TRUE(CoilIsOn());

  SendSwitch(kSwitch, 0);
  Tick(10);
  TEST_ASSERT_TRUE_MESSAGE(
      CoilIsOn(), "switch toggles inside minPulseTime must be ignored");

  Tick(60);
  TEST_ASSERT_FALSE(CoilIsOn());
}

void test_stuck_fast_switch_cannot_hold_the_coil_past_max_pulse(void) {
  // The dangerous case. The switch stays closed - welded contacts, a ball
  // resting on a target - and the coil must still be cut.
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, /*maxPT*/ 100, 0, 0,
                          kSwitch);
  PowerOn();

  SendSwitch(kSwitch, 1);
  TEST_ASSERT_TRUE(CoilIsOn());

  Tick(150);  // switch still closed throughout
  TEST_ASSERT_FALSE_MESSAGE(
      CoilIsOn(), "a stuck fast switch must not hold a coil past maxPulseTime");
}

void test_coil_does_not_refire_until_the_switch_reopens(void) {
  // After a max-pulse cutoff the coil must stay off until the switch has
  // physically reopened. Without this latch, a stuck switch would refire the
  // coil on its very next reported closure - the overheating scenario.
  //
  // The latch is only consulted when a switch-closed *event* arrives, so this
  // test must deliver one. Merely ticking update() proves nothing, because
  // update() never fires a coil.
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, /*maxPT*/ 100, 0, 0,
                          kSwitch);
  PowerOn();

  SendSwitch(kSwitch, 1);
  Tick(150);
  TEST_ASSERT_FALSE(CoilIsOn());

  // The switch is still closed and the reader reports it again. This is the
  // assertion that matters: the coil must refuse to refire.
  SendSwitch(kSwitch, 1);
  TEST_ASSERT_FALSE_MESSAGE(
      CoilIsOn(), "coil refired while the stuck switch was still closed");

  Tick(200);
  SendSwitch(kSwitch, 1);
  TEST_ASSERT_FALSE_MESSAGE(
      CoilIsOn(), "coil refired later while the switch was still closed");

  // Released and pressed again: a genuine new hit, so it may fire.
  SendSwitch(kSwitch, 0);
  Tick(10);
  SendSwitch(kSwitch, 1);
  TEST_ASSERT_TRUE_MESSAGE(
      CoilIsOn(), "a fresh switch closure must be able to fire the coil again");
}

// --- power gating -----------------------------------------------------------

void test_no_coil_fires_before_power_is_on(void) {
  // HighPowerOffAware gates every output until EVENT_RUN arrives.
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, 100, 0, 0, 0);

  SendSolenoid(1);

  TEST_ASSERT_FALSE_MESSAGE(CoilIsOn(),
                            "outputs must stay dead until high power is on");
}

void test_off_releases_every_output(void) {
  g_pwm->registerSolenoid(kPort, kCoilNumber, kPower, 0, 0, 0, 0, 0);
  PowerOn();
  SendSolenoid(1);
  TEST_ASSERT_TRUE(CoilIsOn());

  g_pwm->off();

  TEST_ASSERT_FALSE_MESSAGE(CoilIsOn(), "off() must de-energise every output");
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_host_on_energises_the_coil);
  RUN_TEST(test_host_off_before_min_pulse_defers_the_release);
  RUN_TEST(test_max_pulse_time_forces_the_coil_off);
  RUN_TEST(test_hold_power_reduces_duty_after_activation);

  RUN_TEST(test_fast_switch_fires_the_coil_locally);
  RUN_TEST(test_fast_switch_release_inside_min_pulse_is_deferred);
  RUN_TEST(test_stuck_fast_switch_cannot_hold_the_coil_past_max_pulse);
  RUN_TEST(test_coil_does_not_refire_until_the_switch_reopens);

  RUN_TEST(test_no_coil_fires_before_power_is_on);
  RUN_TEST(test_off_releases_every_output);

  return UNITY_END();
}
