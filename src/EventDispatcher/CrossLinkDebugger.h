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

  static void debug(const char *format, ...);

  static bool active;
};

#endif
