#include "CrossLinkDebugger.h"

CrossLinkDebugger::CrossLinkDebugger()
{
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    if (get_core_num() == 0)
    {
        rp2040.idleOtherCore();
        Serial.println("PPUC IO_16_8_1");
        Serial.print("PPUC board #");
        // Read bordID. Ideal value at 10bit resolution: (DIP+1)*1023*2/35 -> 58.46 to 935.3
        Serial.println(16 - ((int)((analogRead(28) + 29.23) / 58.46)));
        Serial.println("PPUC core #0 started");
#endif
        Serial.println("PPUC CrossLinkDebugger");
        Serial.println("----------------------");
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
        rp2040.resumeOtherCore();
    }
    else
    {
        rp2040.idleOtherCore();
        Serial.println("PPUC core #1 started");
        rp2040.resumeOtherCore();
    }
#endif
}

void CrossLinkDebugger::handleEvent(Event *event)
{
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    rp2040.idleOtherCore();
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
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    rp2040.resumeOtherCore();
#endif
}

void CrossLinkDebugger::handleEvent(ConfigEvent *event)
{
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    rp2040.idleOtherCore();
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
    Serial.print(", value(DEC) ");
    Serial.print(event->value, DEC);
    Serial.print(", value(HEX) ");
    Serial.println(event->value, HEX);
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    rp2040.resumeOtherCore();
#endif
}
