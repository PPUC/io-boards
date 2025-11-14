/**
  PPUCTimings.h
  Created by Markus Kalkbrenner.
*/

#ifndef PPUC_TIMINGS_h
#define PPUC_TIMINGS_h

#define WAIT_FOR_EFFECT_CONTROLLER_RESET 3000  // 3 seconds
#define WAIT_FOR_SERIAL_DEBUGGER_TIMEOUT 1000  // 1 second
#define WAIT_FOR_IO_BOARD_BOOT 1000            // 1 second

#define WAIT_FOR_IO_BOARD_RESET                                          \
  (WAIT_FOR_SERIAL_DEBUGGER_TIMEOUT + WAIT_FOR_EFFECT_CONTROLLER_RESET + \
   WAIT_FOR_IO_BOARD_BOOT)  // 5 seconds

#endif
