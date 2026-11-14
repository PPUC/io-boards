/*
  SessionGate.h

  Which host frames belong to the session, and so to its epoch.

  Play more pinball!
*/
#ifndef EVENTDISPATCHER_SessionGate_h
#define EVENTDISPATCHER_SessionGate_h

#include <stdint.h>

#include "../PPUCProtocolV2.h"

// What a board does with a host frame, given the frame's epoch and its own.
enum class EpochGate : uint8_t {
  kProcess,         // in the session, or not part of one
  kProcessWithGap,  // from another epoch, used anyway, sequence gap noted
  kDrop,            // from another epoch: ignored, sequence gap noted
};

// Administration - version, stats, firmware update - stands outside every
// session: it is how the host learns what it is talking to before there is
// one, and how it asks a board what happened after the session broke down.
// Gating it on the epoch made it fail exactly then: a board starts at epoch 0
// and the host at 1, so before the first setup frame every version query went
// unanswered, and after a board restarted mid-game so did every stats query.
//
// Board-local config arrives before setup too, and is used whatever its epoch.
inline EpochGate HostFrameEpochGate(ppuc::v2::FrameType frameType,
                                    uint8_t incomingEpoch,
                                    uint8_t currentEpoch) {
  if (frameType == ppuc::v2::kFrameAdmin) {
    return EpochGate::kProcess;
  }
  if (incomingEpoch == currentEpoch) {
    return EpochGate::kProcess;
  }
  if (frameType == ppuc::v2::kFrameConfig) {
    return EpochGate::kProcessWithGap;
  }
  return EpochGate::kDrop;
}

#endif
