#include "CrossLinkDebugger.h"

CrossLinkDebugger::CrossLinkDebugger() {
    Serial.println("PPUC CrossLinkDebugger");
    Serial.println("----------------------");
}

void CrossLinkDebugger::handleEvent(Event* event) {
    Serial.print("handleEvent: sourceId ");
    Serial.print(event->sourceId);
    Serial.print(", eventId ");
    Serial.print(event->eventId, DEC);
    Serial.print(", value ");
    Serial.println(event->value, DEC);
}

void CrossLinkDebugger::handleEvent(ConfigEvent* event) {
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
}
