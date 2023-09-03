#include "EventDispatcher.h"

EventDispatcher::EventDispatcher()
{
}

void EventDispatcher::setRS485ModePin(int pin)
{
    rs485 = true;
    rs485Pin = pin;
    pinMode(rs485Pin, OUTPUT);
    digitalWrite(rs485Pin, LOW); // Read.
}

void EventDispatcher::setBoard(byte b)
{
    board = b;
}

void EventDispatcher::setMultiCoreCrossLink(MultiCoreCrossLink *mccl)
{
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    multiCoreCrossLink = mccl;
    multiCore = true;
#endif
}

MultiCoreCrossLink *EventDispatcher::getMultiCoreCrossLink()
{
    return multiCoreCrossLink;
}

void EventDispatcher::setCrossLinkSerial(HardwareSerial &reference)
{
    hwSerial[0] = (HardwareSerial *)&reference;
    crossLink = 0;
}

void EventDispatcher::addCrossLinkSerial(HardwareSerial &reference)
{
    hwSerial[++crossLink] = (HardwareSerial *)&reference;
    hwSerial[crossLink]->begin(115200);
}

void EventDispatcher::addListener(EventListener *eventListener)
{
    addListener(eventListener, EVENT_SOURCE_ANY);
}

void EventDispatcher::addListener(EventListener *eventListener, char sourceId)
{
    if (numListeners < (MAX_EVENT_LISTENERS - 1))
    {
        eventListeners[++numListeners] = eventListener;
        eventListenerFilters[numListeners] = sourceId;
    }
}

void EventDispatcher::dispatch(Event *event)
{
    if (stackCounter < (EVENT_STACK_SIZE - 1))
    {
        stackEvents[++stackCounter] = event;

        if (event->localFast)
        {
            for (byte i = 0; i <= numListeners; i++)
            {
                if (event->sourceId == eventListenerFilters[i] || EVENT_SOURCE_ANY == eventListenerFilters[i])
                {
                    eventListeners[i]->handleEvent(event);
                }
            }
        }
    }
    else
    {
        // Too many events stacked, delete the event and free the memory.
        delete event;
    }
}

void EventDispatcher::callListeners(Event *event, int sender, bool flush)
{
    if (!event->localFast)
    {
        for (byte i = 0; i <= numListeners; i++)
        {
            if (event->sourceId == eventListenerFilters[i] || EVENT_SOURCE_ANY == eventListenerFilters[i])
            {
                eventListeners[i]->handleEvent(event);
            }
        }
    }

    if (!rs485 || flush)
    {
        // Send to other micro controller. But only if there's room left in write buffer. Otherwise the program will be
        // blocked. The buffer gets full if the data is not fetched by the other controller for any reason.
        // @todo Possible optimization to check hwSerial->availableForWrite() >= 6 failed on Arduino for unknown reason.

        if (crossLink != -1 /* && hwSerial->availableForWrite() >= 6 */)
        {
            msg[0] = (byte) 255;
            msg[1] = event->sourceId;
            msg[2] = highByte(event->eventId);
            msg[3] = lowByte(event->eventId);
            msg[4] = event->value;
            msg[5] = (byte) 255;

            for (int i = 0; i <= crossLink; i++)
            {
                if (i != sender)
                {
                    hwSerial[i]->write(msg, 6);
                }
            }
        }

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
        if (multiCore && sender != -1 && event->sourceId != EVENT_NULL)
        {
            multiCoreCrossLink->pushEventNonBlocking(event);
        }
#endif
    }

    // delete the event and free the memory
    delete event;
}

void EventDispatcher::callListeners(ConfigEvent *event, int sender)
{
    for (byte i = 0; i <= numListeners; i++)
    {
        if (EVENT_CONFIGURATION == eventListenerFilters[i] || EVENT_SOURCE_ANY == eventListenerFilters[i])
        {
            eventListeners[i]->handleEvent(event);
        }
    }

    if (sender != -1)
    {
        if (crossLink != -1 /* && hwSerial->availableForWrite() >= 6 */)
        {
            cmsg[0] = (byte) 255;
            cmsg[1] = event->sourceId;
            cmsg[2] = event->boardId;
            cmsg[3] = event->topic;
            cmsg[4] = event->index;
            cmsg[5] = event->key;
            cmsg[6] = event->value >> 24;
            cmsg[7] = (event->value >> 16) & 0xff;
            cmsg[8] = (event->value >> 8) & 0xff;
            cmsg[9] = event->value & 0xff;
            cmsg[10] = (byte) 255;

            for (int i = 0; i <= crossLink; i++)
            {
                if (i != sender)
                {
                    hwSerial[i]->write(cmsg, 11);
                }
            }
        }

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
        if (multiCoreCrossLink)
        {
            multiCoreCrossLink->pushConfigEvent(event);
        }
#endif
    }

    // delete the event and free the memory
    delete event;
}

void EventDispatcher::update()
{
    if (!rs485)
    {
        while (stackCounter >= 0)
        {
            Event *event = stackEvents[stackCounter--];
            // Integer MAX_CROSS_LINKS is always higher than crossLinks, so this parameters means "no sender, send to all".
            callListeners(event, MAX_CROSS_LINKS, false);
        }
    }

    for (int i = 0; i <= crossLink; i++)
    {
        if (hwSerial[i]->available() >= 6)
        {
            // digitalWrite(25, LOW); // Write.
            byte startByte = hwSerial[i]->read();
            if (startByte == 255)
            {
                byte sourceId = hwSerial[i]->read();
                if (sourceId != 0)
                {
                    if (sourceId == EVENT_CONFIGURATION)
                    {
                        // Config Event has 11 bytes, 2 bytes are already parsed above.
                        while (hwSerial[i]->available() < 9)
                        {
                        }

                        // We have a ConfigEvent.
                        byte boardId = hwSerial[i]->read();
                        byte topic = hwSerial[i]->read();
                        byte index = hwSerial[i]->read();
                        byte key = hwSerial[i]->read();
                        int value =
                            (hwSerial[i]->read() << 24) +
                            (hwSerial[i]->read() << 16) +
                            (hwSerial[i]->read() << 8) +
                            hwSerial[i]->read();
                        byte stopByte = hwSerial[i]->read();
                        if (stopByte == 255)
                        {
                            callListeners(new ConfigEvent(boardId, topic, index, key, value), i);
                        }
                    }
                    else
                    {
                        word eventId = word(hwSerial[i]->read(), hwSerial[i]->read());
                        if (eventId != 0)
                        {
                            byte value = hwSerial[i]->read();
                            byte stopByte = hwSerial[i]->read();
                            if (stopByte == 255)
                            {
                                if (sourceId == EVENT_POLL_EVENTS)
                                {
                                    if (board == value)
                                    {
                                        if (rs485)
                                        {
                                            digitalWrite(rs485Pin, HIGH); // Write.
                                        }
                                        while (stackCounter >= 0)
                                        {
                                            Event *event = stackEvents[stackCounter--];
                                            // Integer MAX_CROSS_LINKS is always higher than crossLinks, so this parameters means "no sender, send to all".
                                            callListeners(event, MAX_CROSS_LINKS, true);
                                        }
                                        // Send NULL event to indicate that transmission is complete.
                                        callListeners(new Event(EVENT_NULL), MAX_CROSS_LINKS, true);
                                        if (rs485)
                                        {
                                            digitalWrite(rs485Pin, LOW); // Read.
                                        }
                                    }
                                }
                                else
                                {
                                    callListeners(new Event((char)sourceId, eventId, value), i, false);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    if (multiCoreCrossLink)
    {
        if (multiCoreCrossLink->eventAvailable())
        {
            Event *event;
            if (multiCoreCrossLink->popEventNonBlocking(event))
            {
                callListeners(event, -1, false);
            }
        }

        if (multiCoreCrossLink->configEventAvailable())
        {
            ConfigEvent *configEvent;
            if (multiCoreCrossLink->popConfigEventNonBlocking(configEvent))
            {
                callListeners(configEvent, -1);
            }
        }
    }
#endif
}
