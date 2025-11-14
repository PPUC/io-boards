#include "CrossLinkDebugger.h"

bool CrossLinkDebugger::active = false;

CrossLinkDebugger::CrossLinkDebugger() {
  if (get_core_num() == 0) {
    rp2040.idleOtherCore();
    Serial.println("PPUC IO_16_8_1");
    Serial.print("PPUC board #");
    // Read bordID. Ideal value at 10bit resolution: (DIP+1)*1023*2/35 -> 58.46
    // to 935.3
    Serial.println(16 - ((int)((analogRead(28) + 29.23) / 58.46)));
    Serial.println("PPUC core #0 started");
    Serial.println("PPUC CrossLinkDebugger");
    Serial.println("----------------------");
    rp2040.resumeOtherCore();
  } else {
    rp2040.idleOtherCore();
    Serial.println("PPUC core #1 started");
    rp2040.resumeOtherCore();
  }

  CrossLinkDebugger::active = true;
}

void CrossLinkDebugger::handleEvent(Event *event) {
  rp2040.idleOtherCore();
  Serial.print("Core ");
  Serial.print(get_core_num(), DEC);
  Serial.print(" ");
  Serial.print("handleEvent: sourceId ");
  Serial.print(event->sourceId);
  Serial.print(", eventId ");
  Serial.print(event->eventId, DEC);
  Serial.print(", value ");
  Serial.println(event->value, DEC);
  rp2040.resumeOtherCore();
}

void CrossLinkDebugger::handleEvent(ConfigEvent *event) {
  rp2040.idleOtherCore();
  Serial.print("Core ");
  Serial.print(get_core_num(), DEC);
  Serial.print(" ");
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
  rp2040.resumeOtherCore();
}

void CrossLinkDebugger::debug(const char *format, ...) {
  if (CrossLinkDebugger::active) {
    char buffer[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    rp2040.idleOtherCore();
    Serial.print("Core ");
    Serial.print(get_core_num(), DEC);
    Serial.print(" ");
    Serial.print("debug: ");
    Serial.println(buffer);
    rp2040.resumeOtherCore();
  }
}
