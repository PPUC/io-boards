/*
  BoardDiagnostics.h

  What the board counts about itself, for the host to read over the bus.

  Play more pinball!
*/
#ifndef BoardDiagnostics_h
#define BoardDiagnostics_h

#include <stdint.h>

#include "PPUCProtocolV2.h"

// Indexed by ppuc::v2::DiagnosticsField. Only ever incremented or raised, from
// core 0 and its timer interrupt, and read out by EventDispatcher for a
// kAdminDiagnosticsQuery. A word written by one and read by the other is atomic
// on the RP2040, which is all a counter needs.
//
// This is what the USB debug mode used to be for. Counting costs a few cycles
// where the events happen; nothing is sent until the host asks.
inline volatile uint32_t g_diagnostics[ppuc::v2::kDiagnosticsFieldCount] = {0};

inline void CountDiagnostic(ppuc::v2::DiagnosticsField field) {
  g_diagnostics[field] = g_diagnostics[field] + 1;
}

inline void RaiseDiagnostic(ppuc::v2::DiagnosticsField field, uint32_t value) {
  if (value > g_diagnostics[field]) {
    g_diagnostics[field] = value;
  }
}

#endif
