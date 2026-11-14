/*
  ResetCause.h

  Why the board last started, as reported in the stats report.

  Play more pinball!
*/
#ifndef ResetCause_h
#define ResetCause_h

#include <stdint.h>

#include "PPUCProtocolV2.h"

// Set once at boot, read when the host asks for the stats.
inline volatile uint8_t g_resetCause = ppuc::v2::kResetCauseNotReported;

// What a deliberate reboot leaves in a watchdog scratch register, which
// survives a watchdog reboot but not a loss of power: a marker with the cause
// in the low byte. Scratch 4 to 7 belong to the boot ROM and the SDK.
constexpr uint32_t kRebootMarkerMagic = 0x50504300u;  // "PPC"
constexpr uint32_t kRebootMarkerMask = 0xFFFFFF00u;
constexpr uint8_t kRebootMarkerScratch = 3;

inline uint32_t RebootMarker(uint8_t cause) {
  return kRebootMarkerMagic | cause;
}

// The cause, from what the chip says about the last reset.
//
// A watchdog reboot sets the watchdog's reason register, and a power-on or a
// reset pin clears it: the chip reset bits keep whatever the last chip-level
// reset was, so they only count when the watchdog was not involved. The
// marker is only trusted together with the watchdog, so one left over from
// before a power cut means nothing.
inline uint8_t ResetCauseFrom(bool watchdogCaused, uint32_t marker,
                              bool hadPowerOn, bool hadRunPin,
                              bool hadDebugger) {
  if (watchdogCaused) {
    if ((marker & kRebootMarkerMask) == kRebootMarkerMagic) {
      const uint8_t cause = static_cast<uint8_t>(marker & 0xFFu);
      switch (cause) {
        case ppuc::v2::kResetCauseMainLoopStalled:
        case ppuc::v2::kResetCauseResetFrame:
        case ppuc::v2::kResetCauseFirmwareUpdate:
          return cause;
        default:
          break;
      }
    }
    return ppuc::v2::kResetCauseWatchdogOther;
  }
  if (hadDebugger) return ppuc::v2::kResetCauseDebugger;
  if (hadRunPin) return ppuc::v2::kResetCauseRunPin;
  if (hadPowerOn) return ppuc::v2::kResetCausePowerOn;
  return ppuc::v2::kResetCauseNotReported;
}

// Implemented on the board only, in ResetCause.cpp.
//
// Records why the board is about to reboot itself. Call right before the
// reboot, or before dispatching the event that leads to one.
void NoteRebootCause(uint8_t cause);
// Works out why the board started, clears the marker and sets g_resetCause.
void DetermineResetCause();

#endif
