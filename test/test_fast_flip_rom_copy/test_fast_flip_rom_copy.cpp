// The ROM's own copy of a pulse the board already fired from its switch.
//
// A fast-flip output fires the moment its switch closes. The ROM sees the same
// switch later and fires the same coil itself; the host passes that on. Obeyed,
// the copy's "off" dropped a flipper the player was still holding, and its late
// "on" fired a sling a second time. What must still get through is everything
// the ROM does on its own: the coil test, a ball search, a coil fired with no
// switch in front of it.

#include <unity.h>

#include "Arduino.h"
#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "HardwareStubs.h"
#include "IODevices/PwmDevices.h"

namespace {

constexpr byte kHoldPort = 20;
constexpr byte kHoldNumber = 46;  // a flipper hold winding
constexpr byte kButton = 112;
constexpr byte kSlingPort = 21;
constexpr byte kSlingNumber = 9;
constexpr byte kSlingSwitch = 61;

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

void SendRom(byte coilNumber, uint8_t value) {
  Event event(EVENT_SOURCE_SOLENOID, coilNumber, value);
  g_pwm->handleEvent(&event);
}

void Tick(uint32_t ms) {
  stubs::AdvanceMillis(ms);
  g_pwm->update();
}

bool IsOn(byte port) { return stubs::AnalogValue(port) > 0; }

void RegisterHold() {
  g_pwm->registerSolenoid(kHoldPort, kHoldNumber, 255, /*minPT*/ 0,
                          /*maxPT*/ 0, /*hP*/ 0, /*hPAT*/ 0, kButton);
}

void RegisterSling() {
  g_pwm->registerSolenoid(kSlingPort, kSlingNumber, 192, /*minPT*/ 0,
                          /*maxPT*/ 150, /*hP*/ 0, /*hPAT*/ 0, kSlingSwitch);
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

void test_the_roms_off_does_not_drop_a_held_flipper(void) {
  RegisterHold();
  PowerOn();

  SendSwitch(kButton, 1);
  TEST_ASSERT_TRUE(IsOn(kHoldPort));

  // The ROM's copies of flips, and the ends of copies from earlier presses,
  // arriving while the button is held. Each "off" used to drop the flipper
  // and spend one of the board's re-drives; after five the board waited for
  // the button, and the finger fell -- the up and down a player saw.
  for (int n = 0; n < MAX_STOP_REDRIVES_PER_PRESS + 2; n++) {
    Tick(30);
    SendRom(kHoldNumber, 1);
    Tick(30);
    SendRom(kHoldNumber, 0);
    TEST_ASSERT_TRUE_MESSAGE(
        IsOn(kHoldPort),
        "the ROM's off must not drop a flipper the player holds");
    Tick(1);
    TEST_ASSERT_TRUE_MESSAGE(IsOn(kHoldPort),
                             "and it must still be up afterwards");
  }

  SendSwitch(kButton, 0);
  Tick(1);
  TEST_ASSERT_FALSE_MESSAGE(IsOn(kHoldPort), "releasing the button drops it");
}

void test_a_late_rom_on_after_release_does_not_flip_again(void) {
  RegisterHold();
  PowerOn();

  SendSwitch(kButton, 1);
  Tick(40);
  SendSwitch(kButton, 0);
  Tick(1);
  TEST_ASSERT_FALSE(IsOn(kHoldPort));

  // The ROM saw the press late, and fires once the button is already up.
  Tick(60);
  SendRom(kHoldNumber, 1);
  TEST_ASSERT_FALSE_MESSAGE(
      IsOn(kHoldPort), "the ROM's late copy must not raise the flipper again");
}

void test_a_sling_hit_fires_once(void) {
  RegisterSling();
  PowerOn();

  SendSwitch(kSlingSwitch, 1);
  TEST_ASSERT_TRUE(IsOn(kSlingPort));
  Tick(8);
  SendSwitch(kSlingSwitch, 0);
  Tick(1);
  TEST_ASSERT_FALSE(IsOn(kSlingPort));

  Tick(40);
  SendRom(kSlingNumber, 1);
  TEST_ASSERT_FALSE_MESSAGE(
      IsOn(kSlingPort), "the ROM's copy of the kick must not fire it again");
  Tick(20);
  SendRom(kSlingNumber, 0);
  TEST_ASSERT_FALSE(IsOn(kSlingPort));
}

void test_the_rom_can_fire_a_sling_on_its_own(void) {
  // A ball search, or the coil test: no switch in front of it.
  RegisterSling();
  PowerOn();

  SendRom(kSlingNumber, 1);
  TEST_ASSERT_TRUE_MESSAGE(IsOn(kSlingPort),
                           "a ROM fire of its own must get through");
  Tick(20);
  SendRom(kSlingNumber, 0);
  TEST_ASSERT_FALSE_MESSAGE(IsOn(kSlingPort), "and so must its end");
}

void test_the_rom_can_fire_a_sling_long_after_a_hit(void) {
  RegisterSling();
  PowerOn();

  SendSwitch(kSlingSwitch, 1);
  Tick(8);
  SendSwitch(kSlingSwitch, 0);
  Tick(FAST_SWITCH_ROM_COPY_WINDOW_MS + 1000);

  SendRom(kSlingNumber, 1);
  TEST_ASSERT_TRUE_MESSAGE(
      IsOn(kSlingPort), "a ball search seconds after a hit is the ROM's own");
}

void test_the_rom_can_kick_a_ball_resting_on_a_sling_switch(void) {
  // The switch stays closed, the board gave up at the maximum pulse time and
  // waits for a release that does not come. That is what a ball search is for.
  RegisterSling();
  PowerOn();

  SendSwitch(kSlingSwitch, 1);
  Tick(151);
  TEST_ASSERT_FALSE(IsOn(kSlingPort));
  Tick(FAST_SWITCH_ROM_COPY_WINDOW_MS + 1000);

  SendRom(kSlingNumber, 1);
  TEST_ASSERT_TRUE_MESSAGE(
      IsOn(kSlingPort),
      "the ROM must be able to kick a ball resting on the switch");
}

void test_the_rom_ends_its_own_pulse(void) {
  // The flipper coil test: the ROM fires the hold winding with no button.
  RegisterHold();
  PowerOn();

  SendRom(kHoldNumber, 1);
  TEST_ASSERT_TRUE(IsOn(kHoldPort));
  Tick(50);
  SendRom(kHoldNumber, 0);
  TEST_ASSERT_FALSE_MESSAGE(IsOn(kHoldPort),
                            "a ROM pulse ends when the ROM ends it");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_roms_off_does_not_drop_a_held_flipper);
  RUN_TEST(test_a_late_rom_on_after_release_does_not_flip_again);
  RUN_TEST(test_a_sling_hit_fires_once);
  RUN_TEST(test_the_rom_can_fire_a_sling_on_its_own);
  RUN_TEST(test_the_rom_can_fire_a_sling_long_after_a_hit);
  RUN_TEST(test_the_rom_can_kick_a_ball_resting_on_a_sling_switch);
  RUN_TEST(test_the_rom_ends_its_own_pulse);
  return UNITY_END();
}
