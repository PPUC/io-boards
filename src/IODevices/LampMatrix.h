/*
  LampMatrix.h
  Created by Markus Kalkbrenner, 2026.

  The output stage of Out_8x10: a lamp matrix of high-side columns by low-side
  rows, plus lamps wired straight to a single output.

  Lamps are on or off. The board is meant for the LEDs of an original lamp
  matrix, and nothing here uses the PWM hardware: half of this board's
  low-side outputs share their PWM channels with its high-side outputs, so
  dimming one would drive the other.

  Play more pinball!
*/

#ifndef LampMatrix_h
#define LampMatrix_h

#include <Arduino.h>

#include "../EventDispatcher/Event.h"
#include "../EventDispatcher/EventDispatcher.h"
#include "../PPUCBoardTypes.h"
#include "LampMatrixLogic.h"
#include "PioAllocation.h"
#include "hardware/pio.h"
#include "pico/time.h"

class LampMatrix : public EventListener {
 public:
  LampMatrix(EventDispatcher* eD, const ppuc::board::Profile& p);

  bool setNumRows(uint8_t n) { return logic.setNumRows(n); }
  bool registerLamp(byte position, byte number);
  bool registerDirect(byte pin, byte number);
  void resetConfig();

  // Lamps follow the game. While the board is not running they are dark, the
  // way PWM outputs are, but they keep tracking what the host says.
  void setRunning(bool run);

  void handleEvent(Event* event);

  void handleEvent(ConfigEvent* event) {}

 private:
  static bool onFeed(struct repeating_timer* t);
  void feed();
  void start();
  void stop();
  void claimPins();

  LampMatrixLogic logic;
  uint32_t lampPins = 0;
  uint8_t basePin = 0;
  uint8_t pinSpan = 0;
  uint8_t columns = 0;

  bool active = false;
  bool started = false;
  volatile bool lit = false;
  volatile bool pinsTaken = false;
  volatile uint8_t nextColumn = 0;
  PIO pio = nullptr;
  uint sm = 0;
  uint programOffset = 0;
  struct repeating_timer feedTimer;
  EventDispatcher* _eventDispatcher;
};

#endif
