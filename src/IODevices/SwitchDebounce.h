#ifndef SWITCH_DEBOUNCE_H
#define SWITCH_DEBOUNCE_H

#include <stdint.h>

#include "../EventDispatcher/Event.h"

// Whether a switch edge that is waiting may be accepted now.
//
// Standard switches accept an edge once the input has held the new state for
// the whole debounce window: a closure shorter than that never happened.
//
// Fast-flip switches close at once, which is decided where the edge arrives,
// and only come here for an opening, or for a closing that was too soon after
// the last accepted one and goes as soon as that one is a window old.
//
// A switch that holds an output on - a flipper button, whose winding stays up
// for as long as it is pressed - has to hold an opening for the window too. It
// used to be enough that the previous accepted opening was a window ago, which
// let the first bounce after a press through as a release: the board dropped
// the flipper and fired it again on the next contact, up to a hundred times a
// second, and the player saw a weak or fluttering flipper.
//
// A switch that only pulses its output - a sling, a jet - keeps the old rule,
// an opening a window after the previous one. Its leaf chatters through the
// kick, and holding the opening kept it reading closed, so the board drove the
// coil to its maximum pulse time, and the kick shook the switch into the next.
//
// `pendingSinceUs` is when the input took the waiting state, `lastAcceptedUs`
// when this switch last accepted an edge in the same direction; 0 means never.
inline bool SwitchEdgeDue(uint8_t debounceMode, bool holdsOutput, bool closing,
                          uint32_t nowUs, uint32_t pendingSinceUs,
                          uint32_t lastAcceptedUs, uint32_t debounceUs) {
  const bool mustHold = debounceMode == SWITCH_DEBOUNCE_STANDARD ||
                        (holdsOutput && !closing);
  if (mustHold) {
    return pendingSinceUs == 0 || (nowUs - pendingSinceUs) >= debounceUs;
  }
  return lastAcceptedUs == 0 || (nowUs - lastAcceptedUs) >= debounceUs;
}

#endif
