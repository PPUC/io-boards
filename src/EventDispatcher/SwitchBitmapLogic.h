/*
  SwitchBitmapLogic.h

  How switch events and other boards' switch frames go into this board's
  dense switch bitmaps. Hardware-free, so the native tests cover it.

  Play more pinball!
*/
#ifndef EVENTDISPATCHER_SwitchBitmapLogic_h
#define EVENTDISPATCHER_SwitchBitmapLogic_h

#include <stddef.h>
#include <stdint.h>

#include "../PPUCProtocolV2.h"

// A switch event at one bitmap position.
//
// states        what this board believes about every switch
// localReport   what it reports as its own in its next switch frame
// owned         the positions that are its own, which other boards' frames
//               leave alone
//
// Only a switch this board reads itself claims its position. A switch event
// synthesized from another board's frame used to claim it as well: the frame
// set a flag while it dispatched, but the event only reached this function
// once the queue was processed, after the flag had been cleared again. From
// the first change of another board's switch on, this board took that switch
// for its own, ignored every later frame for it, and kept its first state
// until the next resync. On Dirty Harry the coin door is on board 1, so the
// other boards stopped seeing it open once they had seen it close - the one
// thing the door interlock is there for.
//
// Returns whether the local report changed, which is when the caller queues a
// report snapshot.
inline bool RecordSwitchPosition(uint8_t* states, uint8_t* localReport,
                                 uint8_t* owned, uint16_t index, bool closed,
                                 bool fromOtherBoard) {
  ppuc::v2::SetBitmapBit(states, index, closed);
  if (fromOtherBoard) {
    return false;
  }
  const bool reportChanged = ppuc::v2::GetBitmapBit(localReport, index) != closed;
  ppuc::v2::SetBitmapBit(localReport, index, closed);
  ppuc::v2::SetBitmapBit(owned, index, true);
  return reportChanged;
}

// Another board's switch frame.
//
// Calls onChange(index, closed) for every position this board does not own
// whose state differs from what it last knew, then takes the frame's state
// for those positions. Its own positions are left as they are: another
// board's frame carries nothing at a position it does not own.
template <typename OnChange>
inline void MergeOtherBoardSwitchFrame(uint8_t* states, const uint8_t* owned,
                                       const uint8_t* incoming,
                                       uint16_t switchBits,
                                       OnChange onChange) {
  for (uint16_t n = 0; n < switchBits; ++n) {
    if (ppuc::v2::GetBitmapBit(owned, n)) {
      continue;
    }
    const bool oldState = ppuc::v2::GetBitmapBit(states, n);
    const bool newState = ppuc::v2::GetBitmapBit(incoming, n);
    if (oldState != newState) {
      onChange(n, newState);
    }
  }
  const size_t switchBytes = ppuc::v2::BitsToBytes(switchBits);
  for (size_t i = 0; i < switchBytes; ++i) {
    states[i] = static_cast<uint8_t>((states[i] & owned[i]) |
                                     (incoming[i] & static_cast<uint8_t>(~owned[i])));
  }
}

#endif
