/*
  CrossLinkDebugger.h
  Created by Markus Kalkbrenner, 2020-2021.

  Play more pinball!
*/

#ifndef CROSSLINKDEBUGGER_h
#define CROSSLINKDEBUGGER_h

#include <Arduino.h>
#include <stdio.h>

#include "Event.h"
#include "EventListener.h"

class CrossLinkDebugger : public EventListener {
 public:
  CrossLinkDebugger();

  void handleEvent(Event *event);
  void handleEvent(ConfigEvent *event);

  // Only in a USB debug build (PPUC_USB_DEBUG). Everywhere else it compiles
  // to nothing: printing over USB from the coil and switch code, with the other
  // core idled meanwhile, changed exactly the timing it was meant to show.
#ifdef PPUC_USB_DEBUG
  static void debug(const char *format, ...);
#else
  static void debug(const char *, ...) {}
#endif

  static bool active;
};

#endif
