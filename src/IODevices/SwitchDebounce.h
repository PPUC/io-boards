#ifndef SWITCH_DEBOUNCE_H
#define SWITCH_DEBOUNCE_H

#include <stdint.h>

#include "../EventDispatcher/Event.h"

// Whether a switch edge that is waiting may be accepted now.
//
// Standard switches accept an edge once the input has held the new state for
// the whole debounce window: a closure shorter than that never happened.
//
// Fast-flip switches - flipper buttons, slings, jets - close at once, which is
// decided where the edge arrives, and only come here for an opening. An
// opening has to hold for the window as well. It used to be enough that the
// previous accepted opening was a window ago, which let the first bounce after
// a press through as a release: the board dropped the flipper and fired it
// again on the next contact, up to a hundred times a second, and the player saw
// a weak or fluttering flipper. A closing that lands here was too soon after
// the last accepted closing, and goes as soon as that one is a window old.
//
// `pendingSinceUs` is when the input took the waiting state, `lastAcceptedUs`
// when this switch last accepted an edge in the same direction; 0 means never.
inline bool SwitchEdgeDue(uint8_t debounceMode, bool closing, uint32_t nowUs,
                          uint32_t pendingSinceUs, uint32_t lastAcceptedUs,
                          uint32_t debounceUs) {
  const bool mustHold = debounceMode == SWITCH_DEBOUNCE_STANDARD || !closing;
  if (mustHold) {
    return pendingSinceUs == 0 || (nowUs - pendingSinceUs) >= debounceUs;
  }
  return lastAcceptedUs == 0 || (nowUs - lastAcceptedUs) >= debounceUs;
}

#endif
