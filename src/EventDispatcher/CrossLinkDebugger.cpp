#include "CrossLinkDebugger.h"

// https://stackoverflow.com/questions/20310000/error-iso-c-forbids-in-class-initialization-of-non-const-static-member
bool CrossLinkDebugger::lock = true;

CrossLinkDebugger::CrossLinkDebugger()
{
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    if (get_core_num() == 0)
    {
        Serial.println("PPUC IO_16_8_1");
        Serial.print("PPUC board #");
        Serial.println(16 - ((int)((analogRead(28) + 30) / 60)));
#endif
        Serial.println("PPUC CrossLinkDebugger");
        Serial.println("----------------------");
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    }
    else
    {
        delayMicroseconds(500);
    }
#endif
    lock = false;
}

void CrossLinkDebugger::handleEvent(Event *event)
{
    while (lock)
    {
    }
    lock = true;
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    Serial.print("Core ");
    Serial.print(get_core_num(), DEC);
    Serial.print(" ");
#endif
    Serial.print("handleEvent: sourceId ");
    Serial.print(event->sourceId);
    Serial.print(", eventId ");
    Serial.print(event->eventId, DEC);
    Serial.print(", value ");
    Serial.println(event->value, DEC);
    lock = false;
}

void CrossLinkDebugger::handleEvent(ConfigEvent *event)
{
    while (lock)
    {
    }
    lock = true;
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    Serial.print("Core ");
    Serial.print(get_core_num(), DEC);
    Serial.print(" ");
#endif
    Serial.print("handleConfigEvent: boardId ");
    Serial.print(event->boardId, DEC);
    Serial.print(", topic ");
    Serial.print(event->topic, DEC);
    Serial.print(", index ");
    Serial.print(event->index, DEC);
    Serial.print(", key ");
    Serial.print(event->key, DEC);
    Serial.print(", value(HEX) ");
    Serial.println(event->value, HEX);
    lock = false;
}
