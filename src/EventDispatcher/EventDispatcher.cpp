#include "EventDispatcher.h"

#include <string.h>

EventDispatcher::EventDispatcher() {
  for (uint16_t i = 0; i < ppuc::v2::kMaxCoilBits; ++i) {
    coilIndexToNumber[i] = i;
  }
  for (uint16_t i = 0; i < ppuc::v2::kMaxLampBits; ++i) {
    lampIndexToNumber[i] = i;
  }
  for (uint16_t i = 0; i < ppuc::v2::kMaxSwitchBits; ++i) {
    switchIndexToNumber[i] = i;
  }
}

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
    while (!eventQueue.empty()) {
      Event *e = eventQueue.front();
      eventQueue.pop();
      delete e;
    }
  }

  eventQueue.push(event);

  if (event->localFast) {
    for (byte i = 0; i <= numListeners; i++) {
      if (event->sourceId == eventListenerFilters[i] ||
          EVENT_SOURCE_ANY == eventListenerFilters[i]) {
        eventListeners[i]->handleEvent(event);
      }
    }
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

  if (event->sourceId == EVENT_SOURCE_SWITCH) {
    updateSwitchBitmap(event);
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

bool EventDispatcher::readBytes(byte *buffer, size_t len) {
  size_t offset = 0;
  uint32_t start = micros();
  while (offset < len) {
    if (hwSerial->available() > 0) {
      buffer[offset++] = hwSerial->read();
      continue;
    }

    if ((micros() - start) > 8000) {
      return false;
    }
  }

  return true;
}

int16_t EventDispatcher::findMappedIndex(const uint16_t* table, uint16_t count,
                                         uint16_t number) {
  for (uint16_t i = 0; i < count; ++i) {
    if (table[i] == number) {
      return (int16_t)i;
    }
  }
  return -1;
}

void EventDispatcher::updateSwitchBitmap(Event *event) {
  int16_t mappedIndex =
      findMappedIndex(switchIndexToNumber, runtimeConfig.switchBits,
                      event->eventId);
  if (mappedIndex < 0) {
    return;
  }

  ppuc::v2::SetBitmapBit(switchStates, (uint16_t)mappedIndex,
                         event->value != 0);
}

void EventDispatcher::applyOutputStates(const byte *coils, size_t coilBytes,
                                        const byte *lamps, size_t lampBytes) {
  for (uint16_t n = 0; n < runtimeConfig.coilBits; ++n) {
    bool oldState = ppuc::v2::GetBitmapBit(outputCoils, n);
    bool newState = ppuc::v2::GetBitmapBit(coils, n);
    if (oldState != newState) {
      callListeners(
          new Event(EVENT_SOURCE_SOLENOID, coilIndexToNumber[n],
                    newState ? 1 : 0),
          true, false);
    }
  }
  memcpy(outputCoils, coils, coilBytes);

  for (uint16_t n = 0; n < runtimeConfig.lampBits; ++n) {
    bool oldState = ppuc::v2::GetBitmapBit(outputLamps, n);
    bool newState = ppuc::v2::GetBitmapBit(lamps, n);
    if (oldState != newState) {
      callListeners(
          new Event(EVENT_SOURCE_LIGHT, lampIndexToNumber[n], newState ? 1 : 0),
          true, false);
    }
  }
  memcpy(outputLamps, lamps, lampBytes);
}

void EventDispatcher::sendSwitchStateFrame(byte nextBoard) {
  const size_t switchBytes = ppuc::v2::BitsToBytes(runtimeConfig.switchBits);
  const size_t frameBytes =
      ppuc::v2::kHeaderBytes + switchBytes + ppuc::v2::kCrcBytes;

  byte frame[ppuc::v2::kHeaderBytes + ppuc::v2::kMaxSwitchBytes +
             ppuc::v2::kCrcBytes];
  frame[0] = ppuc::v2::kSyncByte;
  frame[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameSwitchState,
                                           ppuc::v2::kFlagKeyframe);
  frame[2] = nextBoard;
  frame[3] = txSequence++;
  memcpy(&frame[4], switchStates, switchBytes);

  uint16_t crc =
      ppuc::v2::Crc16Ccitt(frame, ppuc::v2::kHeaderBytes + switchBytes);
  frame[4 + switchBytes] = highByte(crc);
  frame[5 + switchBytes] = lowByte(crc);

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, frameBytes);
  hwSerial->flush();
  digitalWrite(rs485Pin, LOW);  // Read.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  lastPoll = millis();
}

bool EventDispatcher::handleV2Frame() {
  if (hwSerial->available() < (int)ppuc::v2::kHeaderBytes) {
    return false;
  }

  if (hwSerial->peek() != ppuc::v2::kSyncByte) {
    return false;
  }

  if (!readBytes(v2Buffer, ppuc::v2::kHeaderBytes)) {
    return false;
  }

  ppuc::v2::FrameType frameType = ppuc::v2::ExtractType(v2Buffer[1]);
  size_t payloadBytes = 0;
  switch (frameType) {
    case ppuc::v2::kFrameSetup:
      payloadBytes = ppuc::v2::kSetupPayloadBytes;
      break;
    case ppuc::v2::kFrameMapping:
      payloadBytes = ppuc::v2::kMappingPayloadBytes;
      break;
    case ppuc::v2::kFrameOutputState:
      payloadBytes = ppuc::v2::BitsToBytes(runtimeConfig.coilBits) +
                     ppuc::v2::BitsToBytes(runtimeConfig.lampBits);
      break;
    case ppuc::v2::kFrameHeartbeat:
    case ppuc::v2::kFrameError:
      payloadBytes = 0;
      break;
    default:
      return false;
  }

  if (!readBytes(&v2Buffer[ppuc::v2::kHeaderBytes],
                 payloadBytes + ppuc::v2::kCrcBytes)) {
    return false;
  }

  const size_t crcOffset = ppuc::v2::kHeaderBytes + payloadBytes;
  uint16_t receivedCrc =
      word(v2Buffer[crcOffset], v2Buffer[crcOffset + 1]);
  uint16_t expectedCrc =
      ppuc::v2::Crc16Ccitt(v2Buffer, ppuc::v2::kHeaderBytes + payloadBytes);
  if (receivedCrc != expectedCrc) {
    return true;
  }

  if (frameType == ppuc::v2::kFrameSetup) {
    ppuc::v2::RuntimeConfig newConfig;
    newConfig.coilBits = word(v2Buffer[4], v2Buffer[5]);
    newConfig.lampBits = word(v2Buffer[6], v2Buffer[7]);
    newConfig.switchBits = word(v2Buffer[8], v2Buffer[9]);
    if (ppuc::v2::IsValidRuntimeConfig(newConfig)) {
      runtimeConfig = newConfig;
      memset(outputCoils, 0, sizeof(outputCoils));
      memset(outputLamps, 0, sizeof(outputLamps));
      memset(switchStates, 0, sizeof(switchStates));
      for (uint16_t i = 0; i < runtimeConfig.coilBits; ++i) {
        coilIndexToNumber[i] = i;
      }
      for (uint16_t i = 0; i < runtimeConfig.lampBits; ++i) {
        lampIndexToNumber[i] = i;
      }
      for (uint16_t i = 0; i < runtimeConfig.switchBits; ++i) {
        switchIndexToNumber[i] = i;
      }
    }
    return true;
  }

  if (frameType == ppuc::v2::kFrameMapping) {
    const uint8_t domain = v2Buffer[4];
    const uint16_t index = word(v2Buffer[6], v2Buffer[7]);
    const uint16_t number = word(v2Buffer[8], v2Buffer[9]);

    if (domain == ppuc::v2::kDomainCoil && index < runtimeConfig.coilBits) {
      coilIndexToNumber[index] = number;
    } else if (domain == ppuc::v2::kDomainLamp &&
               index < runtimeConfig.lampBits) {
      lampIndexToNumber[index] = number;
    } else if (domain == ppuc::v2::kDomainSwitch &&
               index < runtimeConfig.switchBits) {
      switchIndexToNumber[index] = number;
    }

    return true;
  }

  if (frameType == ppuc::v2::kFrameOutputState) {
    const size_t coilBytes = ppuc::v2::BitsToBytes(runtimeConfig.coilBits);
    const size_t lampBytes = ppuc::v2::BitsToBytes(runtimeConfig.lampBits);
    applyOutputStates(&v2Buffer[4], coilBytes, &v2Buffer[4 + coilBytes],
                      lampBytes);

    if (v2Buffer[2] == board) {
      sendSwitchStateFrame((byte)((board + 1) % ppuc::v2::kMaxBoards));
    }
    return true;
  }

  return true;
}

bool EventDispatcher::handleLegacyFrame() {
  if (hwSerial->available() < 7) {
    return false;
  }

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
            callListeners(new ConfigEvent(boardId, topic, index, key, value),
                          true);
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

                while (!eventQueue.empty()) {
                  Event *e = eventQueue.front();
                  eventQueue.pop();
                  callListeners(e, true, true);
                }

                // Send NULL event to indicate that transmission is complete.
                callListeners(new Event(EVENT_NULL, 1, board), false, true);

                lastPoll = millis();

                // Flush the serial buffer and wait until done.
                hwSerial->flush();
                digitalWrite(rs485Pin, LOW);  // Read.
                // Wait until the RS485 converter switched back to read mode.
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

  return success;
}

void EventDispatcher::update() {
  if (!rs485) {  // We're on Core1, the EffectController. Transmit stacked
                 // events to Core0.
    while (!eventQueue.empty()) {
      Event *e = eventQueue.front();
      eventQueue.pop();
      callListeners(e, true, false);
    }
  } else {
    while (!eventQueue.empty()) {
      Event *e = eventQueue.front();
      eventQueue.pop();
      callListeners(e, true, false);
    }

    while (hwSerial->available() > 0) {
      int firstByte = hwSerial->peek();
      if (firstByte == ppuc::v2::kSyncByte) {
        if (!handleV2Frame()) {
          break;
        }
      } else if (firstByte == 255) {
        if (!handleLegacyFrame()) {
          break;
        }
      } else {
        // Desync/noise, consume one byte and continue.
        hwSerial->read();
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
