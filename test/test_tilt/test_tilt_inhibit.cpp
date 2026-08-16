// Tilt-inhibit tests for PwmDevices.
//
// Tilt has to drop the flippers and nothing else. Two failure modes matter, and
// both are asserted here:
//
//   * a flipper the player is still HOLDING must go dead. This is the one the
//     firmware used to get wrong: update() reconciled fastSwitchClosed against
//     the live switch bitmap and re-fired the output with no power gate at all,
//     so the flipper came back within one board loop.
//   * the ball-recovery coils must stay drivable. A tilt that killed all high
//     power would take the outhole kicker with it and strand the ball, and the
//     machine would need a human with the glass off.
//
// The host owns the tilt decision (warning counting is policy), so the board
// watches a designated switch number rather than the tilt bob itself. That
// mirrors how coinDoorClosedSwitch already works.

#include <unity.h>

#include "Arduino.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "HardwareStubs.h"
#include "IODevices/PwmDevices.h"

namespace {

constexpr byte kFlipperPort = 19;
constexpr byte kFlipperCoil = 7;
constexpr byte kKickerPort = 20;
constexpr byte kKickerCoil = 8;
constexpr byte kPower = 255;
constexpr uint16_t kFlipperButton = 33;
constexpr uint16_t kTiltSwitch = 250;

EventDispatcher* g_dispatcher = nullptr;
PwmDevices* g_pwm = nullptr;

void ConfigureTiltSwitch(uint16_t number) {
  ConfigEvent event(/*boardId*/ 1, CONFIG_TOPIC_TILT_SWITCH, /*index*/ 0, CONFIG_TOPIC_NUMBER, number);
  g_pwm->handleEvent(&event);
}

void PowerOn() {
  Event runEvent(EVENT_RUN, 1, 1);
  g_pwm->handleEvent(&runEvent);
}

void SendSwitch(uint16_t number, uint8_t value) {
  stubs::SetSwitchState(number, value != 0);
  Event event(EVENT_SOURCE_SWITCH, number, value);
  g_pwm->handleEvent(&event);
}

void SendSolenoid(byte coil, uint8_t value) {
  Event event(EVENT_SOURCE_SOLENOID, coil, value);
  g_pwm->handleEvent(&event);
}

void Tick(uint32_t ms) {
  stubs::AdvanceMillis(ms);
  g_pwm->update();
}

bool FlipperIsOn() { return stubs::AnalogValue(kFlipperPort) > 0; }
bool KickerIsOn() { return stubs::AnalogValue(kKickerPort) > 0; }

// A flipper on a fast-flip switch, and a host-driven outhole kicker.
void RegisterMachine() {
  g_pwm->registerSolenoid(kFlipperPort, kFlipperCoil, kPower, 0, 0, 0, 0,
                          kFlipperButton);
  g_pwm->registerSolenoid(kKickerPort, kKickerCoil, kPower, 0, /*maxPT*/ 150, 0,
                          0, 0);
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

void test_flippers_work_when_not_tilted(void) {
  RegisterMachine();
  ConfigureTiltSwitch(kTiltSwitch);
  PowerOn();

  SendSwitch(kFlipperButton, 1);
  TEST_ASSERT_TRUE(FlipperIsOn());
}

void test_tilt_drops_a_held_flipper(void) {
  // The regression. The player is holding the button when tilt latches; the
  // flipper must go dead and stay dead.
  RegisterMachine();
  ConfigureTiltSwitch(kTiltSwitch);
  PowerOn();

  SendSwitch(kFlipperButton, 1);
  TEST_ASSERT_TRUE(FlipperIsOn());

  SendSwitch(kTiltSwitch, 1);
  TEST_ASSERT_FALSE_MESSAGE(FlipperIsOn(),
                            "tilt must cut a flipper the player is holding");

  // The old bug: update() re-read the switch bitmap and re-fired the output.
  for (int i = 0; i < 10; ++i) {
    Tick(5);
    TEST_ASSERT_FALSE_MESSAGE(
        FlipperIsOn(), "a held flipper must not come back while tilted");
  }
}

void test_tilt_does_not_touch_the_ball_recovery_coils(void) {
  // If this fails, a tilted machine cannot return its own ball.
  RegisterMachine();
  ConfigureTiltSwitch(kTiltSwitch);
  PowerOn();

  SendSwitch(kTiltSwitch, 1);

  SendSolenoid(kKickerCoil, 1);
  TEST_ASSERT_TRUE_MESSAGE(KickerIsOn(),
                           "the outhole kicker must still fire while tilted");
}

void test_pressing_the_flipper_while_tilted_does_nothing(void) {
  RegisterMachine();
  ConfigureTiltSwitch(kTiltSwitch);
  PowerOn();

  SendSwitch(kTiltSwitch, 1);
  SendSwitch(kFlipperButton, 1);
  TEST_ASSERT_FALSE(FlipperIsOn());

  Tick(20);
  TEST_ASSERT_FALSE(FlipperIsOn());
}

void test_a_held_button_needs_a_fresh_press_after_tilt_clears(void) {
  // Without the wait-for-release, a player still holding the button when tilt
  // clears at the next ball start gets an immediate flip. The maxPulseTime path
  // already demands a release for the same reason.
  RegisterMachine();
  ConfigureTiltSwitch(kTiltSwitch);
  PowerOn();

  SendSwitch(kFlipperButton, 1);
  SendSwitch(kTiltSwitch, 1);
  TEST_ASSERT_FALSE(FlipperIsOn());

  SendSwitch(kTiltSwitch, 0);
  Tick(20);
  TEST_ASSERT_FALSE_MESSAGE(
      FlipperIsOn(),
      "a still-held button must not fire the flipper when tilt clears");

  SendSwitch(kFlipperButton, 0);
  Tick(5);
  SendSwitch(kFlipperButton, 1);
  TEST_ASSERT_TRUE_MESSAGE(FlipperIsOn(),
                           "a fresh press after tilt clears must fire");
}

void test_flippers_return_after_tilt_clears(void) {
  RegisterMachine();
  ConfigureTiltSwitch(kTiltSwitch);
  PowerOn();

  SendSwitch(kTiltSwitch, 1);
  SendSwitch(kTiltSwitch, 0);
  Tick(5);

  SendSwitch(kFlipperButton, 1);
  TEST_ASSERT_TRUE(FlipperIsOn());
}

void test_power_off_also_drops_a_held_flipper(void) {
  // The same underlying bug, reached through game-on rather than through tilt.
  RegisterMachine();
  ConfigureTiltSwitch(kTiltSwitch);
  PowerOn();

  SendSwitch(kFlipperButton, 1);
  TEST_ASSERT_TRUE(FlipperIsOn());

  Event runOff(EVENT_RUN, 1, 0);
  g_pwm->handleEvent(&runOff);
  TEST_ASSERT_FALSE(FlipperIsOn());

  for (int i = 0; i < 10; ++i) {
    Tick(5);
    TEST_ASSERT_FALSE_MESSAGE(
        FlipperIsOn(), "a held flipper must not come back with power off");
  }
}

void test_an_unconfigured_tilt_switch_never_inhibits(void) {
  // A machine with no tilt switch declared must behave exactly as before.
  RegisterMachine();
  PowerOn();

  SendSwitch(kTiltSwitch, 1);
  SendSwitch(kFlipperButton, 1);
  TEST_ASSERT_TRUE_MESSAGE(
      FlipperIsOn(),
      "switch 250 must be an ordinary switch when no tilt switch is configured");
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_flippers_work_when_not_tilted);
  RUN_TEST(test_tilt_drops_a_held_flipper);
  RUN_TEST(test_tilt_does_not_touch_the_ball_recovery_coils);
  RUN_TEST(test_pressing_the_flipper_while_tilted_does_nothing);
  RUN_TEST(test_a_held_button_needs_a_fresh_press_after_tilt_clears);
  RUN_TEST(test_flippers_return_after_tilt_clears);
  RUN_TEST(test_power_off_also_drops_a_held_flipper);
  RUN_TEST(test_an_unconfigured_tilt_switch_never_inhibits);

  return UNITY_END();
}
