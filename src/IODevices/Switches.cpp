#include "Switches.h"

void Switches::registerSwitch(byte p, byte n)
{
    if (last < (MAX_SWITCHES - 1))
    {
        if (p >= 15 && p <= 18) { 
            // Set mid power output as input.
            pinMode(p, OUTPUT);
            digitalWrite(HIGH);
            delayMicroseconds(100);
            digitalWrite(LOW);
        }  
        
        pinMode(p, INPUT);
        port[++last] = p;
        number[last] = n;
        toggled[last] = false;
        state[last] = !digitalRead(p);
#if defined(USB_DEBUG) && (defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040))
        rp2040.idleOtherCore();
        Serial.print("Register Switch ");
        Serial.print(n, DEC);
        Serial.print(" on port ");
        Serial.println(p, DEC);
        rp2040.resumeOtherCore();
#endif
    }
}

void Switches::update()
{
    // Wait for SWITCH_DEBOUNCE milliseconds to debounce the switches. That covers the edge case that a switch was hit
    // right before the last polling of events. After SWITCH_DEBOUNCE milliseconds every switch is allowed to toggle
    // once until the events get polled again.
    if (millis() - _ms >= SWITCH_DEBOUNCE)
    {
        for (int i = 0; i <= last; i++)
        {
            if (!toggled[i])
            {
                bool new_state = !digitalRead(port[i]);
                if (new_state != state[i])
                {
                    state[i] = new_state;
                    toggled[i] = true;
                    // Dispatch all switch events as "local fast".
                    // If a PWM output registered to it, we have "fast flip". Useful for flippers, kick backs, jets and
                    // sling shots.
                    _eventDispatcher->dispatch(new Event(EVENT_SOURCE_SWITCH, word(0, number[i]), state[i], true));
                }
            }
        }
    }
}

void Switches::handleEvent(Event *event)
{
    switch (event->sourceId)
    {
    case EVENT_POLL_EVENTS:
        if (boardId == (byte)event->value)
        {
            // This I/O board has been polled for events, so all current switch states are transmitted. Reset switch
            // debounce timer and toggles.
            _ms = millis();
            for (int i = 0; i <= last; i++)
            {
                toggled[i] = false;
            }
        }
        break;

    case EVENT_READ_SWITCHES:
        // The CPU requested all current states.
        for (int i = 0; i <= last; i++)
        {
            // Send all states of switches that haven't been toggled since last poll.
            if (!toggled[i])
            {
                toggled[i] = true;
                _eventDispatcher->dispatch(new Event(EVENT_SOURCE_SWITCH, word(0, number[i]), state[i]));
            }
        }
        break;
    }
}
