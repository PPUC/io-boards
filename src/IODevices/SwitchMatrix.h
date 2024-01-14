/*
  SwitchMatrix_h.
  Created by Markus Kalkbrenner, 2023.
*/

#ifndef SwitchMatrix_h
#define SwitchMatrix_h

#include "../EventDispatcher/Event.h"
#include "../EventDispatcher/EventDispatcher.h"
#include "../PPUC.h"

#ifndef MAX_COLUMNS
#define MAX_COLUMNS 10
#endif

#ifndef MAX_ROWS
#define MAX_ROWS 8
#endif

class SwitchMatrix : public EventListener {
 public:
  SwitchMatrix(byte bId, EventDispatcher *eD) {
    boardId = bId;
    platform = PLATFORM_LIBPINMAME;

    reset();

    _ms = millis();
    _eventDispatcher = eD;
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);
  }

  void registerColumn(byte p, byte n);
  void registerRow(byte p, byte n);

  void setActiveLow();
  void setPulseTime(byte pT);

  void update();
  void reset();

  void handleEvent(Event *event);

  void handleEvent(ConfigEvent *event) {}

 private:
  byte boardId;
  byte platform;
  byte pulseTime;
  byte pauseTime;
  bool activeLow;
  bool active;

  unsigned long _ms;

  int8_t columns[MAX_COLUMNS];
  int8_t rows[MAX_ROWS];
  bool state[MAX_COLUMNS][MAX_ROWS] = {0};
  bool toggled[MAX_COLUMNS][MAX_ROWS] = {0};
  byte column = 0;

  EventDispatcher *_eventDispatcher;
};

#endif
