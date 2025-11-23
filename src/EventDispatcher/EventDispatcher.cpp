#include "EventDispatcher.h"

EventDispatcher::EventDispatcher() {}

void EventDispatcher::setRS485ModePin(int pin) {
  rs485 = true;
  rs485Pin = pin;
}

void EventDispatcher::setBoard(byte b) { board = b; }

void EventDispatcher::setMultiCoreCrossLink(MultiCoreCrossLink *mccl) {
  multiCoreCrossLink = mccl;
  multiCore = true;
}

MultiCoreCrossLink *EventDispatcher::getMultiCoreCrossLink() {
  return multiCoreCrossLink;
}

void EventDispatcher::setCrossLinkSerial(HardwareSerial &reference) {
  hwSerial = (HardwareSerial *)&reference;
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

void EventDispatcher::callListeners(Event *event, bool sendToOtherCore,
                                    bool sendToRS485) {
  if (!event->localFast) {
    for (byte i = 0; i <= numListeners; i++) {
      if (event->sourceId == eventListenerFilters[i] ||
          EVENT_SOURCE_ANY == eventListenerFilters[i]) {
        eventListeners[i]->handleEvent(event);
      }
    }
  }

  if (rs485 && sendToRS485) {
    msg[0] = 0b11111111;
    msg[1] = event->sourceId;
    msg[2] = highByte(event->eventId);
    msg[3] = lowByte(event->eventId);
    msg[4] = event->value;
    msg[5] = 0b10101010;
    msg[6] = 0b01010101;

    hwSerial->write(msg, 7);
  }

  if (multiCore && sendToOtherCore && event->sourceId != EVENT_NULL) {
    multiCoreCrossLink->pushEvent(event);
  }

  // delete the event and free the memory
  delete event;
}

void EventDispatcher::callListeners(ConfigEvent *event, bool sendToOtherCore) {
  for (byte i = 0; i <= numListeners; i++) {
    if (EVENT_CONFIGURATION == eventListenerFilters[i] ||
        EVENT_SOURCE_ANY == eventListenerFilters[i]) {
      eventListeners[i]->handleEvent(event);
    }
  }

  if (multiCoreCrossLink && sendToOtherCore && event->boardId == board) {
    multiCoreCrossLink->pushConfigEvent(event);
  }

  // delete the event and free the memory
  delete event;
}

void EventDispatcher::update() {
  if (!rs485) {  // We're on Core1, the EffectController. Transmit stacked
                 // events to Core0.
    for (int i = 0; i <= stackCounter; i++) {
      Event *event = stackEvents[i];
      callListeners(event, true, false);
    }
    // -1 means empty.
    stackCounter = -1;
  } else {
    if (hwSerial->available() >= 7) {
      bool success = false;

      byte startByte = hwSerial->read();
      if (startByte == 255) {
        byte sourceId = hwSerial->read();
        if (sourceId != 0) {
          if (sourceId == EVENT_CONFIGURATION) {
            // Config Event has 12 bytes, 2 bytes are already parsed above.
            while (hwSerial->available() < 10) {
            }

            // We have a ConfigEvent.
            byte boardId = hwSerial->read();
            byte topic = hwSerial->read();
            byte index = hwSerial->read();
            byte key = hwSerial->read();
            uint32_t value = (((uint32_t)hwSerial->read()) << 24) +
                             (((uint32_t)hwSerial->read()) << 16) +
                             (((uint32_t)hwSerial->read()) << 8) +
                             hwSerial->read();
            byte stopByte = hwSerial->read();
            if (stopByte == 0b10101010) {
              stopByte = hwSerial->read();
              if (stopByte == 0b01010101) {
                success = true;
                callListeners(
                    new ConfigEvent(boardId, topic, index, key, value), true);
              }
            }
          } else {
            word eventId = word(hwSerial->read(), hwSerial->read());
            if (eventId != 0) {
              byte value = hwSerial->read();
              byte stopByte = hwSerial->read();
              if (stopByte == 0b10101010) {
                stopByte = hwSerial->read();
                if (stopByte == 0b01010101) {
                  success = true;
                  callListeners(new Event((char)sourceId, eventId, value), true,
                                false);

                  if (sourceId == EVENT_POLL_EVENTS && board == value) {
                    digitalWrite(rs485Pin, HIGH);  // Write.
                    // Wait until the RS485 converter switched to write mode.
                    delayMicroseconds(RS485_MODE_SWITCH_DELAY);

                    for (int k = 0; k <= stackCounter; k++) {
                      Event *event = stackEvents[k];
                      callListeners(event, true, true);
                    }
                    // -1 means empty.
                    stackCounter = -1;

                    // Send NULL event to indicate that transmission is
                    // complete.
                    callListeners(new Event(EVENT_NULL, 1, board), false, true);

                    lastPoll = millis();

                    // Flush the serial buffer and wait until done.
                    hwSerial->flush();
                    digitalWrite(rs485Pin, LOW);  // Read.
                    // Wait until the RS485 converter switched back to read
                    // mode.
                    delayMicroseconds(RS485_MODE_SWITCH_DELAY);
                  } else if (sourceId == EVENT_RUN) {
                    running = true;
                  }

                } else {
                  if (Serial) {
                    rp2040.idleOtherCore();
                    Serial.print("Received wrong second stop byte ");
                    Serial.println(stopByte, DEC);
                    rp2040.resumeOtherCore();
                  }
                }
              } else {
                if (Serial) {
                  rp2040.idleOtherCore();
                  Serial.print("Received wrong first stop byte ");
                  Serial.println(stopByte, DEC);
                  rp2040.resumeOtherCore();
                }
              }
            } else {
              if (Serial) {
                rp2040.idleOtherCore();
                Serial.print("Received invalid event id ");
                Serial.println(eventId, DEC);
                rp2040.resumeOtherCore();
              }
            }
          }
        } else {
          if (Serial) {
            rp2040.idleOtherCore();
            Serial.print("Received invalid source id ");
            Serial.println(sourceId, DEC);
            rp2040.resumeOtherCore();
          }
        }
      } else {
        if (Serial) {
          rp2040.idleOtherCore();
          Serial.print("Received wrong start byte ");
          Serial.println(startByte, DEC);
          rp2040.resumeOtherCore();
        }
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

        while (hwSerial->available()) {
          byte bits = hwSerial->read();
          if (bits == 0b10101010 && hwSerial->available()) {
            bits = hwSerial->read();
            if (bits == 0b01010101) {
              // Now we should be back in sync.
              break;
            }
          }
        }
      }
    }
  }

  if (multiCoreCrossLink) {
    if (multiCoreCrossLink->eventAvailable()) {
      Event *event = multiCoreCrossLink->popEvent();
      callListeners(event, false, false);
    }

    if (multiCoreCrossLink->configEventAvailable()) {
      ConfigEvent *configEvent = multiCoreCrossLink->popConfigEvent();
      callListeners(configEvent, false);
    }
  }
}

uint32_t EventDispatcher::getLastPoll() {
  if (running) return lastPoll;

  return millis();
}
