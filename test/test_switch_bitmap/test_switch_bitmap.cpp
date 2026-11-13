// How switch events and other boards' switch frames go into a board's bitmaps.
//
// The case this file exists for: a board must keep following another board's
// switch after it has changed once. It did not, because the event it
// synthesized from the frame claimed the switch's position as its own, and a
// board ignores other boards' frames at its own positions. On Dirty Harry
// that left boards 2 to 4 believing the coin door, on board 1, was still
// closed after it had been opened.

#include <unity.h>

#include <string.h>

#include <vector>

#include "EventDispatcher/SwitchBitmapLogic.h"

namespace {

constexpr uint16_t kBits = 55;  // Dirty Harry's switch positions
constexpr uint16_t kDoor = 12;  // a position another board owns
constexpr uint16_t kButton = 40;  // a position this board reads itself

struct Board {
  uint8_t states[ppuc::v2::kMaxSwitchBytes] = {0};
  uint8_t localReport[ppuc::v2::kMaxSwitchBytes] = {0};
  uint8_t owned[ppuc::v2::kMaxSwitchBytes] = {0};

  struct Change {
    uint16_t index;
    bool closed;
  };
  std::vector<Change> dispatched;

  // Another board's frame arrives. Its events are queued, as in the firmware,
  // and only recorded afterwards.
  void receiveFrame(const uint8_t* frame) {
    std::vector<Change> queued;
    MergeOtherBoardSwitchFrame(states, owned, frame, kBits,
                               [&](uint16_t n, bool closed) {
                                 queued.push_back({n, closed});
                               });
    for (const Change& c : queued) {
      dispatched.push_back(c);
      RecordSwitchPosition(states, localReport, owned, c.index, c.closed,
                           /*fromOtherBoard*/ true);
    }
  }

  bool localSwitch(uint16_t index, bool closed) {
    return RecordSwitchPosition(states, localReport, owned, index, closed,
                                /*fromOtherBoard*/ false);
  }
};

void frameWith(uint8_t* frame, uint16_t index, bool closed) {
  memset(frame, 0, ppuc::v2::kMaxSwitchBytes);
  ppuc::v2::SetBitmapBit(frame, index, closed);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_another_boards_switch_is_followed_after_its_first_change(void) {
  Board board;
  uint8_t frame[ppuc::v2::kMaxSwitchBytes];

  frameWith(frame, kDoor, true);
  board.receiveFrame(frame);
  frameWith(frame, kDoor, false);
  board.receiveFrame(frame);

  TEST_ASSERT_EQUAL(2, board.dispatched.size());
  TEST_ASSERT_TRUE(board.dispatched[0].closed);
  TEST_ASSERT_FALSE_MESSAGE(board.dispatched[1].closed,
                            "the door opening must reach this board");
  TEST_ASSERT_FALSE(ppuc::v2::GetBitmapBit(board.states, kDoor));
}

void test_another_boards_switch_is_neither_owned_nor_reported(void) {
  Board board;
  uint8_t frame[ppuc::v2::kMaxSwitchBytes];

  frameWith(frame, kDoor, true);
  board.receiveFrame(frame);

  TEST_ASSERT_TRUE(ppuc::v2::GetBitmapBit(board.states, kDoor));
  TEST_ASSERT_FALSE(ppuc::v2::GetBitmapBit(board.owned, kDoor));
  TEST_ASSERT_FALSE(ppuc::v2::GetBitmapBit(board.localReport, kDoor));
}

void test_a_local_switch_is_owned_reported_and_kept_from_other_frames(void) {
  Board board;
  uint8_t frame[ppuc::v2::kMaxSwitchBytes];

  TEST_ASSERT_TRUE_MESSAGE(board.localSwitch(kButton, true),
                           "a change of its own is a report");
  TEST_ASSERT_TRUE(ppuc::v2::GetBitmapBit(board.owned, kButton));
  TEST_ASSERT_TRUE(ppuc::v2::GetBitmapBit(board.localReport, kButton));

  // Another board's frame has nothing at a position it does not own.
  frameWith(frame, kDoor, false);
  board.receiveFrame(frame);
  TEST_ASSERT_TRUE(ppuc::v2::GetBitmapBit(board.states, kButton));
  TEST_ASSERT_EQUAL(0, board.dispatched.size());
}

void test_a_local_switch_announced_unchanged_is_not_a_report(void) {
  Board board;
  board.localSwitch(kButton, true);
  TEST_ASSERT_FALSE(board.localSwitch(kButton, true));
  TEST_ASSERT_TRUE(board.localSwitch(kButton, false));
}

void test_an_unchanged_frame_dispatches_nothing(void) {
  Board board;
  uint8_t frame[ppuc::v2::kMaxSwitchBytes];
  frameWith(frame, kDoor, true);
  board.receiveFrame(frame);
  board.receiveFrame(frame);
  TEST_ASSERT_EQUAL(1, board.dispatched.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_another_boards_switch_is_followed_after_its_first_change);
  RUN_TEST(test_another_boards_switch_is_neither_owned_nor_reported);
  RUN_TEST(test_a_local_switch_is_owned_reported_and_kept_from_other_frames);
  RUN_TEST(test_a_local_switch_announced_unchanged_is_not_a_report);
  RUN_TEST(test_an_unchanged_frame_dispatches_nothing);
  return UNITY_END();
}
