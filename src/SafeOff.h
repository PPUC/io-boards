/*
  SafeOff.h
  Created by Markus Kalkbrenner, 2026.

  The watchdog's view of the outputs, shared with the drivers that have to
  recover from it.

  Play more pinball!
*/

#ifndef SafeOff_h
#define SafeOff_h

// True while the watchdog is holding this board's outputs off: the main loop
// has stalled, or the host has stopped polling.
//
// The watchdog takes the pins by switching them to plain GPIO and driving them
// low. A PWM output gets its pin back on the next analogWrite(). A driver that
// runs its pins from a PIO block does not, and has to hand them back itself
// once this clears.
extern volatile bool g_outputsForcedOff;

#endif
