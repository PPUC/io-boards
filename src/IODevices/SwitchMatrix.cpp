#include "SwitchMatrix.h"

void SwitchMatrix::setActiveLow()
{
    activeLow = true;
}

void SwitchMatrix::setPulseTime(byte pT)
{
    pulseTime = pT;
}


void SwitchMatrix::registerColumn(byte p, byte n)
{
    if (n > 0 && n < MAX_COLUMNS)
    {
        columns[n - 1] = p;
        pinMode(p, OUTPUT);
    }
}

void SwitchMatrix::registerRow(byte p, byte n)
{
    if (n > 0 && n < MAX_ROWS)
    {
        rows[n - 1] = p;
        pinMode(p, INPUT);
    }
}

void SwitchMatrix::update()
{
    unsigned long ms = millis();
    if (active)
    {
        if (ms > (_ms + (int)(pulseTime / 2)))
        {
            for (int row = 0; row < MAX_ROWS; row++)
            {
                if (rows[row] != -1 && !toggled[column][row])
                {
                    bool new_state = digitalRead(rows[row]);
                    if (new_state != state[column][row])
                    {
                        state[column][row] = new_state;
                        toggled[column][row] = true;

                        word number = (column + 1) * (row + 1);
                        if (platform != PLATFORM_DATA_EAST)
                        {
                            number = ((column + 1) * 10) + (row + 1);
                        }
                        // Dispatch all switch events as "local fast".
                        // If a PWM output registered to it, we have "fast flip". Useful for flippers, kick backs, jets and
                        // sling shots.
                        _eventDispatcher->dispatch(new Event(EVENT_SOURCE_SWITCH, number, state[column][row], true));
                    }
                }
            }
        }

        if (ms > (_ms + pulseTime))
        {
            digitalWrite(columns[column], activeLow);
            active = false;
            _ms = ms;
        }
    }
    else if (!active && (ms > (_ms + pauseTime)))
    {
        column++;
        if (column >= MAX_COLUMNS)
        {
            column = 0;
        }

        // If column is not in use (-1), the next update will increase the column.
        if (columns[column] != -1)
        {
            digitalWrite(columns[column], !activeLow);
            active = true;
            _ms = ms;
        }
    }
}

void SwitchMatrix::handleEvent(Event *event)
{
    switch (event->sourceId)
    {
    case EVENT_POLL_EVENTS:
        if (boardId == (byte)event->value)
        {
            // This I/O board has been polled for events, so all current switch states are transmitted.
            // Reset switch toggles.
            for (int col = 0; col < MAX_COLUMNS; col++)
            {
                for (int row = 0; row < MAX_ROWS; row++)
                {
                    toggled[col][row] = false;
                }
            }
        }
        break;

    case EVENT_READ_SWITCHES:
        // The CPU requested all current states.
        for (int col = 0; col < MAX_COLUMNS; col++)
        {
            if (columns[col] != -1)
            {
                for (int row = 0; row < MAX_ROWS; row++)
                {
                    if (rows[row] != -1)
                    {
                        word number = (column + 1) * (row + 1);
                        if (platform != PLATFORM_DATA_EAST)
                        {
                            number = ((column + 1) * 10) + (row + 1);
                        }
                        _eventDispatcher->dispatch(new Event(EVENT_SOURCE_SWITCH, number, state[column][row]));
                    }
                }
            }
        }
        break;
    }
}
