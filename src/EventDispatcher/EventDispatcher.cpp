#include "EventDispatcher.h"

EventDispatcher::EventDispatcher() {}

void EventDispatcher::setRS485ModePin(int pin) {
  rs485 = true;
  rs485Pin = pin;
  pinMode(rs485Pin, OUTPUT);
  digitalWrite(rs485Pin, LOW);  // Read.
}

void EventDispatcher::setBoard(byte b) { board = b; }

void EventDispatcher::setMultiCoreCrossLink(MultiCoreCrossLink *mccl) {
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
  multiCoreCrossLink = mccl;
  multiCore = true;
#endif
}

MultiCoreCrossLink *EventDispatcher::getMultiCoreCrossLink() {
  return multiCoreCrossLink;
}

void EventDispatcher::setCrossLinkSerial(HardwareSerial &reference) {
  hwSerial[0] = (HardwareSerial *)&reference;
  crossLink = 0;
}

void EventDispatcher::addCrossLinkSerial(HardwareSerial &reference) {
  hwSerial[++crossLink] = (HardwareSerial *)&reference;
  hwSerial[crossLink]->begin(115200);
  while (!hwSerial[crossLink]) {
  }
}

void EventDispatcher::addListener(EventListener *eventListener) {
  addListener(eventListener, EVENT_SOURCE_ANY);
}

void EventDispatcher::addListener(EventListener *eventListener, char sourceId) {
  if (numListeners < (MAX_EVENT_LISTENERS - 1)) {
    eventListeners[++numListeners] = eventListener;
    eventListenerFilters[numListeners] = sourceId;
  }
}

void EventDispatcher::dispatch(Event *event) {
  if (EVENT_RESET == event->sourceId) {
    // Force immediate handling of the reset event. Forget about the others.
    for (int i = 0; i <= stackCounter; i++) {
      if (stackEvents[i]) {
        delete stackEvents[i];
      }
    }
    stackCounter = -1;
  }

  if (stackCounter < (EVENT_STACK_SIZE - 1)) {
    stackEvents[++stackCounter] = event;

    if (event->localFast) {
      for (byte i = 0; i <= numListeners; i++) {
        if (event->sourceId == eventListenerFilters[i] ||
            EVENT_SOURCE_ANY == eventListenerFilters[i]) {
          eventListeners[i]->handleEvent(event);
        }
      }
    }
  } else {
    // Too many events stacked, delete the event and free the memory.
    delete event;
  }
}

void EventDispatcher::callListeners(Event *event, int sender, bool flush) {
  if (!event->localFast) {
    for (byte i = 0; i <= numListeners; i++) {
      if (event->sourceId == eventListenerFilters[i] ||
          EVENT_SOURCE_ANY == eventListenerFilters[i]) {
        eventListeners[i]->handleEvent(event);
      }
    }
  }

  if (!rs485 || flush) {
    // Send to other micro controller. But only if there's room left in write
    // buffer. Otherwise the program will be blocked. The buffer gets full if
    // the data is not fetched by the other controller for any reason.
    // @todo Possible optimization to check hwSerial->availableForWrite() >= 6
    // failed on Arduino for unknown reason.

    if (crossLink != -1 /* && hwSerial->availableForWrite() >= 6 */) {
      msg[0] = 0b11111111;
      msg[1] = event->sourceId;
      msg[2] = highByte(event->eventId);
      msg[3] = lowByte(event->eventId);
      msg[4] = event->value;
      msg[5] = 0b10101010;
      msg[6] = 0b01010101;

      for (int i = 0; i <= crossLink; i++) {
        if (i != sender) {
          hwSerial[i]->write(msg, 7);
        }
      }

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
      if (false && Serial) {
        rp2040.idleOtherCore();
        Serial.print("Sent event: sourceId ");
        Serial.print(event->sourceId);
        Serial.print(", eventId ");
        Serial.print(event->eventId, DEC);
        Serial.print(", value ");
        Serial.println(event->value, DEC);
        rp2040.resumeOtherCore();
      }
#endif
    }
  }

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
  if (multiCore && sender != -1 && event->sourceId != EVENT_NULL) {
    multiCoreCrossLink->pushEvent(event);
  }
#endif

  // delete the event and free the memory
  delete event;
}

void EventDispatcher::callListeners(ConfigEvent *event, int sender) {
  for (byte i = 0; i <= numListeners; i++) {
    if (EVENT_CONFIGURATION == eventListenerFilters[i] ||
        EVENT_SOURCE_ANY == eventListenerFilters[i]) {
      eventListeners[i]->handleEvent(event);
    }
  }

  if (sender != -1) {
    if (crossLink != -1 /* && hwSerial->availableForWrite() >= 6 */) {
      msg[0] = 0b11111111;
      msg[1] = event->sourceId;
      msg[2] = event->boardId;
      msg[3] = event->topic;
      msg[4] = event->index;
      msg[5] = event->key;
      msg[6] = (event->value >> 24) & 0xff;
      msg[7] = (event->value >> 16) & 0xff;
      msg[8] = (event->value >> 8) & 0xff;
      msg[9] = event->value & 0xff;
      msg[10] = 0b10101010;
      msg[11] = 0b01010101;

      for (int i = 0; i <= crossLink; i++) {
        if (i != sender) {
          hwSerial[i]->write(msg, 12);
        }
      }
    }

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
    if (multiCoreCrossLink && event->boardId == board) {
      multiCoreCrossLink->pushConfigEvent(event);
    }
#endif
  }

  // delete the event and free the memory
  delete event;
}

void EventDispatcher::update() {
  if (!rs485) {
    for (int i = 0; i <= stackCounter; i++) {
      Event *event = stackEvents[i];
      // Integer MAX_CROSS_LINKS is always higher than crossLinks, so this
      // parameters means "no sender, send to all".
      callListeners(event, MAX_CROSS_LINKS, false);
    }
    // -1 means empty.
    stackCounter = -1;
  }

  for (int i = 0; i <= crossLink; i++) {
    if (hwSerial[i]->available() >= 7) {
      bool success = false;

      byte startByte = hwSerial[i]->read();
      if (startByte == 255) {
        byte sourceId = hwSerial[i]->read();
        if (sourceId != 0) {
          if (sourceId == EVENT_CONFIGURATION) {
            // Config Event has 12 bytes, 2 bytes are already parsed above.
            while (hwSerial[i]->available() < 10) {
            }

            // We have a ConfigEvent.
            byte boardId = hwSerial[i]->read();
            byte topic = hwSerial[i]->read();
            byte index = hwSerial[i]->read();
            byte key = hwSerial[i]->read();
            uint32_t value = (((uint32_t)hwSerial[i]->read()) << 24) +
                             (((uint32_t)hwSerial[i]->read()) << 16) +
                             (((uint32_t)hwSerial[i]->read()) << 8) +
                             hwSerial[i]->read();
            byte stopByte = hwSerial[i]->read();
            if (stopByte == 0b10101010) {
              stopByte = hwSerial[i]->read();
              if (stopByte == 0b01010101) {
                success = true;
                callListeners(
                    new ConfigEvent(boardId, topic, index, key, value), i);
              }
            }
          } else {
            word eventId = word(hwSerial[i]->read(), hwSerial[i]->read());
            if (eventId != 0) {
              byte value = hwSerial[i]->read();
              byte stopByte = hwSerial[i]->read();
              if (stopByte == 0b10101010) {
                stopByte = hwSerial[i]->read();
                if (stopByte == 0b01010101) {
                  success = true;
                  callListeners(new Event((char)sourceId, eventId, value), i,
                                false);

                  if (sourceId == EVENT_POLL_EVENTS && board == value) {
                    if (rs485) {
                      digitalWrite(rs485Pin, HIGH);  // Write.
                      // Wait until the RS485 converter switched to write mode.
                      delayMicroseconds(RS485_MODE_SWITCH_DELAY);
                    }

                    for (int k = 0; k <= stackCounter; k++) {
                      Event *event = stackEvents[k];
                      // Integer MAX_CROSS_LINKS is always higher than
                      // crossLinks, so this parameters means "no sender, send
                      // to all".
                      callListeners(event, MAX_CROSS_LINKS, true);
                    }
                    // -1 means empty.
                    stackCounter = -1;

                    // Send NULL event to indicate that transmission is
                    // complete.
                    callListeners(new Event(EVENT_NULL, 1, board),
                                  MAX_CROSS_LINKS, true);

                    lastPoll = millis();

                    if (rs485) {
                      // Flush the serial buffer and wait until done.
                      hwSerial[i]->flush();
                      digitalWrite(rs485Pin, LOW);  // Read.
                      // Wait until the RS485 converter switched back to read
                      // mode.
                      delayMicroseconds(RS485_MODE_SWITCH_DELAY);
                    }
                  } else if (sourceId == EVENT_RUN) {
                    running = true;
                  }

                } else {
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
                  if (Serial) {
                    rp2040.idleOtherCore();
                    Serial.print("Received wrong second stop byte ");
                    Serial.println(stopByte, DEC);
                    rp2040.resumeOtherCore();
                  }
#endif
                }
              } else {
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
                if (Serial) {
                  rp2040.idleOtherCore();
                  Serial.print("Received wrong first stop byte ");
                  Serial.println(stopByte, DEC);
                  rp2040.resumeOtherCore();
                }
#endif
              }
            } else {
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
              if (Serial) {
                rp2040.idleOtherCore();
                Serial.print("Received invalid event id ");
                Serial.println(eventId, DEC);
                rp2040.resumeOtherCore();
              }
#endif
            }
          }
        } else {
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
          if (Serial) {
            rp2040.idleOtherCore();
            Serial.print("Received invalid source id ");
            Serial.println(sourceId, DEC);
            rp2040.resumeOtherCore();
          }
#endif
        }
      } else {
#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
        if (Serial) {
          rp2040.idleOtherCore();
          Serial.print("Received wrong start byte ");
          Serial.println(startByte, DEC);
          rp2040.resumeOtherCore();
        }
#endif
        // We didn't receive a start byte. Fake "success" to start over with the
        // next byte.
        success = true;
      }

      if (success) {
        if (error) {
          error = false;
          dispatch(new Event(EVENT_NO_ERROR, 1, board));
        }
      } else {
        error = true;
        dispatch(new Event(EVENT_ERROR, 1, board));

        while (hwSerial[i]->available()) {
          byte bits = hwSerial[i]->read();
          if (bits == 0b10101010 && hwSerial[i]->available()) {
            bits = hwSerial[i]->read();
            if (bits == 0b01010101) {
              // Now we should be back in sync.
              break;
            }
          }
        }
      }
    }
  }

#if defined(ARDUINO_ARCH_MBED_RP2040) || defined(ARDUINO_ARCH_RP2040)
  if (multiCoreCrossLink) {
    if (multiCoreCrossLink->eventAvailable()) {
      Event *event = multiCoreCrossLink->popEvent();
      callListeners(event, -1, false);
    }

    if (multiCoreCrossLink->configEventAvailable()) {
      ConfigEvent *configEvent = multiCoreCrossLink->popConfigEvent();
      callListeners(configEvent, -1);
    }
  }
#endif
}

uint32_t EventDispatcher::getLastPoll() {
  if (running) return lastPoll;

  return millis();
}
