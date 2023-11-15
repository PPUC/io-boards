/*
  SwitchMatrix_h.
  Created by Markus Kalkbrenner, 2023.
*/

#ifndef SwitchMatrix_h
#define SwitchMatrix_h

#include "../PPUC.h"
#include "../EventDispatcher/Event.h"
#include "../EventDispatcher/EventDispatcher.h"

#ifndef MAX_COLUMNS
#define MAX_COLUMNS 10
#endif

#ifndef MAX_ROWS
#define MAX_ROWS 8
#endif

class SwitchMatrix : public EventListener {
public:
    SwitchMatrix(byte bId, EventDispatcher* eD) {
        boardId = bId;
        platform = PLATFORM_LIBPINMAME;
        pulseTime = 2;
        pauseTime = 2;
        activeLow = false;
        active = false;
        
        for (int col = 0; col < MAX_COLUMNS; col++)
        {
            columns[col] = -1;
        } 
        for (int row = 0; row < MAX_ROWS; row++)
        {
            rows[row] = -1;
        }

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

    void handleEvent(Event* event);

    void handleEvent(ConfigEvent* event) {}

private:
    byte boardId;
    byte platform;
    byte pulseTime;
    byte pauseTime;
    bool activeLow;
    bool active;

    unsigned long _ms;

    int columns[MAX_COLUMNS];
    int rows[MAX_ROWS];
    bool state[MAX_COLUMNS][MAX_ROWS] = {0};
    bool toggled[MAX_COLUMNS][MAX_ROWS] = {0};
    byte column = 0;

    EventDispatcher* _eventDispatcher;
};

#endif
