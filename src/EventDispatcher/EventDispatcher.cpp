#include "EventDispatcher.h"

#include <string.h>

namespace {
constexpr uint32_t kSerialBaudRate = ppuc::v2::kBaudRate;

uint32_t FrameWireTimeUs(size_t frameBytes) {
  // Approximate 8N1 UART wire time. Add a small guard so we can safely switch
  // RS485 direction back to RX without relying on HardwareSerial::flush(),
  // which appears to hang in the board-to-host switch reply path.
  const uint32_t bits = static_cast<uint32_t>(frameBytes) * 10;
  return (bits * 1000000u) / kSerialBaudRate + 200;
}
}

EventDispatcher::EventDispatcher() {
  lastPoll = 0;
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

namespace {
void ApplyMaskedBitmap(byte* destination, const byte* source, const byte* mask,
                       size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) {
    destination[i] = static_cast<byte>((destination[i] & ~mask[i]) |
                                       (source[i] & mask[i]));
  }
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

void EventDispatcher::setDebug(bool enabled) { debugEnabled = enabled; }

void EventDispatcher::setNextSwitchBoard(byte boardId) {
  nextSwitchBoard = boardId;
}

void EventDispatcher::setSwitchReplyDelayUs(uint32_t delayUs) {
  switchReplyDelayUs = delayUs;
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

void EventDispatcher::removeListener(EventListener *eventListener) {
  for (byte i = 0; i <= numListeners; i++) {
    if (eventListeners[i] == eventListener) {
      eventListeners[i] = nullptr;
      eventListenerFilters[i] = EVENT_NULL;
    }
  }
}

void EventDispatcher::dispatch(Event *event) {
  if (EVENT_RESET == event->sourceId || EVENT_RESTART == event->sourceId) {
    // Force immediate handling of reset/restart. Forget about any queued
    // runtime/config traffic from the previous session first.
    while (!eventQueue.empty()) {
      Event *e = eventQueue.front();
      eventQueue.pop();
      delete e;
    }
    if (multiCoreCrossLink) {
      multiCoreCrossLink->clearAll();
    }
  }

  eventQueue.push(event);

  if (event->localFast) {
    for (byte i = 0; i <= numListeners; i++) {
      if (!eventListeners[i]) {
        continue;
      }
      if (event->sourceId == eventListenerFilters[i] ||
          EVENT_SOURCE_ANY == eventListenerFilters[i]) {
        eventListeners[i]->handleEvent(event);
      }
    }
  }
}

void EventDispatcher::callListeners(Event *event, bool sendToOtherCore) {
  if (!event->localFast) {
    for (byte i = 0; i <= numListeners; i++) {
      if (!eventListeners[i]) {
        continue;
      }
      if (event->sourceId == eventListenerFilters[i] ||
          EVENT_SOURCE_ANY == eventListenerFilters[i]) {
        eventListeners[i]->handleEvent(event);
      }
    }
  }

  if (multiCore && sendToOtherCore && event->sourceId != EVENT_NULL) {
    if (shouldDropOnCrossCoreBackpressure(event)) {
      if (!multiCoreCrossLink->tryPushEvent(event)) {
        crossCoreEventDrops++;
      }
    } else {
      multiCoreCrossLink->pushEvent(event);
    }
  }

  if (event->sourceId == EVENT_SOURCE_SWITCH) {
    updateSwitchBitmap(event);
  }

  // delete the event and free the memory
  delete event;
}

bool EventDispatcher::shouldDropOnCrossCoreBackpressure(
    const Event* event) const {
  if (!event) {
    return false;
  }

  switch (event->sourceId) {
    case EVENT_SOURCE_SOLENOID:
    case EVENT_SOURCE_LIGHT:
    case EVENT_SOURCE_GI:
    case EVENT_SOURCE_SWITCH:
      return true;
    default:
      return false;
  }
}

void EventDispatcher::callListeners(ConfigEvent *event, bool sendToOtherCore) {
  for (byte i = 0; i <= numListeners; i++) {
    if (!eventListeners[i]) {
      continue;
    }
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

bool EventDispatcher::getSwitchState(uint16_t number) const {
  const int16_t mappedIndex =
      findMappedIndex(switchIndexToNumber, runtimeConfig.switchBits, number);
  if (mappedIndex < 0) {
    return false;
  }

  return ppuc::v2::GetBitmapBit(switchStates, static_cast<uint16_t>(mappedIndex));
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

size_t EventDispatcher::getV2PayloadBytes(ppuc::v2::FrameType frameType) {
  switch (frameType) {
    case ppuc::v2::kFrameSetup:
      return ppuc::v2::kSetupPayloadBytes;
    case ppuc::v2::kFrameMapping:
      return ppuc::v2::kMappingPayloadBytes;
    case ppuc::v2::kFrameConfig:
      return ppuc::v2::kConfigPayloadBytes;
    case ppuc::v2::kFrameConfigAck:
      return ppuc::v2::kConfigAckPayloadBytes;
    case ppuc::v2::kFrameOutputState:
      return ppuc::v2::OutputPayloadBytes(runtimeConfig);
    case ppuc::v2::kFrameSwitchState:
      return ppuc::v2::SwitchPayloadBytes(runtimeConfig);
    case ppuc::v2::kFrameSwitchNoChange:
      return ppuc::v2::SwitchNoChangePayloadBytes();
    case ppuc::v2::kFrameTrigger:
      return ppuc::v2::kTriggerPayloadBytes;
    case ppuc::v2::kFrameHeartbeat:
    case ppuc::v2::kFrameError:
    case ppuc::v2::kFrameReset:
    case ppuc::v2::kFrameRestart:
      return 0;
    default:
      return 0;
  }
}

void EventDispatcher::clearSessionState() {
  runtimeConfig = ppuc::v2::RuntimeConfig();
  runtimeConfigValid = false;
  mappingComplete = false;
  expectedMappingFrames = 0;
  receivedMappingFrames = 0;
  v2RuntimeInitialized = false;
  lastHostSequenceSeen = 0;
  lastHostFrameSequenceSeen = 0;
  lastHostFrameSequenceValid = false;
  sequenceGapDetected = false;
  parserResynced = false;
  transportErrorLatched = false;
  switchOverflow = false;
  applyingRemoteSwitchState = false;
  nextSwitchBoard = ppuc::v2::kNoBoard;
  memset(outputCoils, 0, sizeof(outputCoils));
  memset(outputLamps, 0, sizeof(outputLamps));
  memset(outputGi, 0, sizeof(outputGi));
  memset(switchStates, 0, sizeof(switchStates));
  memset(localReportSwitchStates, 0, sizeof(localReportSwitchStates));
  memset(lastSentLocalReportSwitchStates, 0,
         sizeof(lastSentLocalReportSwitchStates));
  memset(localOwnedSwitchMask, 0, sizeof(localOwnedSwitchMask));
  memset(localSwitchReportHistory, 0, sizeof(localSwitchReportHistory));
  localSwitchReportHead = 0;
  localSwitchReportTail = 0;
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

void EventDispatcher::resetSessionState(
    uint8_t newEpoch, const ppuc::v2::RuntimeConfig& cfg) {
  currentEpoch = newEpoch;
  runtimeConfig = cfg;
  runtimeConfigValid = true;
  expectedMappingFrames = static_cast<uint16_t>(cfg.coilBits + cfg.lampBits +
                                                cfg.switchBits);
  receivedMappingFrames = 0;
  mappingComplete = expectedMappingFrames == 0;
  lastHostSequenceSeen = 0;
  lastHostFrameSequenceSeen = 0;
  lastHostFrameSequenceValid = false;
  sequenceGapDetected = false;
  parserResynced = false;

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

uint8_t EventDispatcher::currentStatusFlags() const {
  uint8_t flags = 0;
  if (runtimeConfigValid && mappingComplete) {
    flags |= ppuc::v2::kStatusInSync;
  }
  if (!runtimeConfigValid) {
    flags |= ppuc::v2::kStatusNeedsSetup;
  }
  if (runtimeConfigValid && !mappingComplete) {
    flags |= ppuc::v2::kStatusMappingIncomplete;
  }
  if (sequenceGapDetected) {
    flags |= ppuc::v2::kStatusSequenceGap;
  }
  if (parserResynced) {
    flags |= ppuc::v2::kStatusParserResynced;
  }
  if (switchOverflow) {
    flags |= ppuc::v2::kStatusSwitchOverflow;
  }
  return flags;
}

void EventDispatcher::clearReportedStatusFlags() {
  sequenceGapDetected = false;
  parserResynced = false;
  switchOverflow = false;
}

bool EventDispatcher::processV2Frame(const byte* frame, size_t payloadBytes) {
  const ppuc::v2::FrameType frameType = ppuc::v2::ExtractType(frame[1]);
  const uint8_t incomingSequence = frame[3];
  const uint8_t incomingEpoch = frame[4];
  const size_t payloadOffset = ppuc::v2::kHeaderBytes;
  const size_t crcOffset = ppuc::v2::kHeaderBytes + payloadBytes;
  uint16_t receivedCrc = word(frame[crcOffset], frame[crcOffset + 1]);
  uint16_t expectedCrc =
      ppuc::v2::Crc16Ccitt(frame, ppuc::v2::kHeaderBytes + payloadBytes);
  if (receivedCrc != expectedCrc) {
    v2RxCrcFail++;
    parserResynced = true;
    if (!transportErrorLatched) {
      dispatch(new Event(EVENT_ERROR, 1, board));
      transportErrorLatched = true;
    }
    return false;
  }
  v2RxFrames++;
  if (transportErrorLatched) {
    dispatch(new Event(EVENT_NO_ERROR, 1, board));
    transportErrorLatched = false;
  }

  const bool hostOriginated =
      frameType == ppuc::v2::kFrameSetup || frameType == ppuc::v2::kFrameMapping ||
      frameType == ppuc::v2::kFrameConfig ||
      frameType == ppuc::v2::kFrameTrigger ||
      frameType == ppuc::v2::kFrameOutputState ||
      frameType == ppuc::v2::kFrameHeartbeat ||
      frameType == ppuc::v2::kFrameError || frameType == ppuc::v2::kFrameReset ||
      frameType == ppuc::v2::kFrameRestart;
  const bool hostOutputFrame = frameType == ppuc::v2::kFrameOutputState;

  auto noteHostSequence = [&]() {
    if (lastHostFrameSequenceValid &&
        static_cast<uint8_t>(lastHostFrameSequenceSeen + 1) !=
            incomingSequence) {
      sequenceGapDetected = true;
    }
    lastHostFrameSequenceSeen = incomingSequence;
    lastHostFrameSequenceValid = true;
    if (hostOutputFrame) {
      lastHostSequenceSeen = incomingSequence;
    }
  };

  if (frameType == ppuc::v2::kFrameSetup) {
    ppuc::v2::RuntimeConfig newConfig;
    newConfig.coilBits = word(frame[payloadOffset], frame[payloadOffset + 1]);
    newConfig.lampBits =
        word(frame[payloadOffset + 2], frame[payloadOffset + 3]);
    newConfig.switchBits =
        word(frame[payloadOffset + 4], frame[payloadOffset + 5]);
    if (ppuc::v2::IsValidRuntimeConfig(newConfig)) {
      resetSessionState(incomingEpoch, newConfig);
      if (!v2RuntimeInitialized) {
        // The v2 host no longer relies on legacy serial control events for
        // startup. Once setup arrives, all config frames have already been
        // applied, so we can enable runtime processing and start switch input
        // capture locally.
        dispatch(new Event(EVENT_RUN, 1, 1));
        dispatch(new Event(EVENT_READ_SWITCHES));
        v2RuntimeInitialized = true;
      }
    }
    return true;
  }

  if (frameType == ppuc::v2::kFrameReset ||
      frameType == ppuc::v2::kFrameRestart) {
    clearSessionState();
    noteHostSequence();
    dispatch(new Event(frameType == ppuc::v2::kFrameReset ? EVENT_RESET
                                                          : EVENT_RESTART));
    // Process reset/restart teardown immediately before consuming any
    // following config frames from the new session. Without this, core 0 can
    // ACK and queue fresh config for core 1 while core 1 is still running the
    // previous WS2812/effects state, which is especially fragile on the first
    // board in the chain after a soft restart.
    while (!eventQueue.empty()) {
      Event* queuedEvent = eventQueue.front();
      eventQueue.pop();
      callListeners(queuedEvent, true);
    }
    return true;
  }

  if (hostOriginated && incomingEpoch != currentEpoch) {
    sequenceGapDetected = true;
    if (frameType == ppuc::v2::kFrameConfig) {
      // Board-local config is session-independent and may arrive before setup.
    } else {
      return true;
    }
  }

  if (hostOriginated) {
    noteHostSequence();
  }

  if (frameType == ppuc::v2::kFrameMapping) {
    if (!runtimeConfigValid || incomingEpoch != currentEpoch) {
      return true;
    }
    const uint8_t domain = frame[payloadOffset];
    const uint16_t index =
        word(frame[payloadOffset + 2], frame[payloadOffset + 3]);
    const uint16_t number =
        word(frame[payloadOffset + 4], frame[payloadOffset + 5]);

    if (domain == ppuc::v2::kDomainCoil && index < runtimeConfig.coilBits) {
      coilIndexToNumber[index] = number;
    } else if (domain == ppuc::v2::kDomainLamp &&
               index < runtimeConfig.lampBits) {
      lampIndexToNumber[index] = number;
    } else if (domain == ppuc::v2::kDomainSwitch &&
               index < runtimeConfig.switchBits) {
      switchIndexToNumber[index] = number;
    }
    if (receivedMappingFrames < expectedMappingFrames) {
      receivedMappingFrames++;
    }
    mappingComplete = receivedMappingFrames >= expectedMappingFrames;
    return true;
  }

  if (frameType == ppuc::v2::kFrameOutputState) {
    if (!runtimeConfigValid || incomingEpoch != currentEpoch) {
      return true;
    }
    forwardSwitchTokenIfSelected(frame[2]);
    const size_t coilBytes = ppuc::v2::BitsToBytes(runtimeConfig.coilBits);
    const size_t lampBytes = ppuc::v2::BitsToBytes(runtimeConfig.lampBits);
    applyOutputStates(&frame[payloadOffset], coilBytes,
                      &frame[payloadOffset + coilBytes], lampBytes,
                      &frame[payloadOffset + coilBytes + lampBytes]);
    return true;
  }

  if (frameType == ppuc::v2::kFrameSwitchState) {
    if (!runtimeConfigValid || incomingEpoch != currentEpoch) {
      return true;
    }
    forwardSwitchTokenIfSelected(frame[2]);
    const size_t switchBytes = ppuc::v2::BitsToBytes(runtimeConfig.switchBits);
    applySwitchStates(&frame[payloadOffset + ppuc::v2::kSwitchStatusBytes],
                      switchBytes);
    return true;
  }

  if (frameType == ppuc::v2::kFrameSwitchNoChange) {
    if (runtimeConfigValid && incomingEpoch == currentEpoch) {
      forwardSwitchTokenIfSelected(frame[2]);
    }
    return true;
  }

  if (frameType == ppuc::v2::kFrameTrigger) {
    if (!runtimeConfigValid || incomingEpoch != currentEpoch) {
      return true;
    }

    const uint8_t source = frame[payloadOffset];
    const uint16_t number =
        word(frame[payloadOffset + 1], frame[payloadOffset + 2]);
    const uint8_t value = frame[payloadOffset + 3];
    callListeners(new Event(source, number, value), true);
    return true;
  }

  if (frameType == ppuc::v2::kFrameConfig) {
    const uint8_t targetBoard = frame[payloadOffset];
    const uint8_t topic = frame[payloadOffset + 1];
    const uint8_t index = frame[payloadOffset + 2];
    const uint8_t key = frame[payloadOffset + 3];
    callListeners(
        new ConfigEvent(targetBoard, topic, index, key,
                        (((uint32_t)frame[payloadOffset + 4]) << 24) |
                            (((uint32_t)frame[payloadOffset + 5]) << 16) |
                            (((uint32_t)frame[payloadOffset + 6]) << 8) |
                            ((uint32_t)frame[payloadOffset + 7])),
        true);
    if (targetBoard == board) {
      sendConfigAckFrame(targetBoard, topic, index, key,
                         ppuc::v2::kConfigAckAccepted);
    }
    return true;
  }

  return true;
}

void EventDispatcher::sendConfigAckFrame(uint8_t boardId, uint8_t topic,
                                         uint8_t index, uint8_t key,
                                         uint8_t status) {
  byte frame[ppuc::v2::kConfigAckFrameBytes];
  frame[0] = ppuc::v2::kSyncByte;
  frame[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameConfigAck,
                                           ppuc::v2::kFlagNone);
  frame[2] = ppuc::v2::kNoBoard;
  frame[3] = txSequence++;
  frame[4] = currentEpoch;
  frame[5] = boardId;
  frame[6] = topic;
  frame[7] = index;
  frame[8] = key;
  frame[9] = status;
  frame[10] = 0;
  frame[11] = 0;
  frame[12] = 0;
  const uint16_t crc =
      ppuc::v2::Crc16Ccitt(frame, ppuc::v2::kHeaderBytes +
                                      ppuc::v2::kConfigAckPayloadBytes);
  frame[13] = highByte(crc);
  frame[14] = lowByte(crc);

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, sizeof(frame));
  delayMicroseconds(FrameWireTimeUs(sizeof(frame)));
  digitalWrite(rs485Pin, LOW);  // Read.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
}

int16_t EventDispatcher::findMappedIndex(const uint16_t* table, uint16_t count,
                                         uint16_t number) const {
  for (uint16_t i = 0; i < count; ++i) {
    if (table[i] == number) {
      return (int16_t)i;
    }
  }
  return -1;
}

void EventDispatcher::updateSwitchBitmap(Event *event) {
  // V2 switch reporting is bitmap-based. Legacy switch events still originate
  // from the existing switch devices/listeners; this method mirrors those
  // events into the dense V2 switch-state RAM bitmap. On token/poll, the board
  // sends this bitmap back to the CPU in one V2 switch frame.
  int16_t mappedIndex =
      findMappedIndex(switchIndexToNumber, runtimeConfig.switchBits,
                      event->eventId);
  if (mappedIndex < 0) {
    return;
  }

  const bool newState = event->value != 0;
  ppuc::v2::SetBitmapBit(switchStates, (uint16_t)mappedIndex, newState);
  if (!applyingRemoteSwitchState) {
    const bool oldLocalState = ppuc::v2::GetBitmapBit(localReportSwitchStates,
                                                      (uint16_t)mappedIndex);
    ppuc::v2::SetBitmapBit(localReportSwitchStates, (uint16_t)mappedIndex,
                           newState);
    ppuc::v2::SetBitmapBit(localOwnedSwitchMask, (uint16_t)mappedIndex, true);

    if (oldLocalState != newState) {
      for (uint8_t repeat = 0; repeat < SWITCH_REPORT_REPEAT_COUNT; ++repeat) {
        const uint8_t nextHead = static_cast<uint8_t>(
            (localSwitchReportHead + 1) % SWITCH_REPORT_HISTORY_SIZE);
        if (nextHead == localSwitchReportTail) {
          switchOverflow = true;
          localSwitchReportTail = static_cast<uint8_t>(
              (localSwitchReportTail + 1) % SWITCH_REPORT_HISTORY_SIZE);
        }
        memcpy(localSwitchReportHistory[localSwitchReportHead],
               localReportSwitchStates, sizeof(localReportSwitchStates));
        localSwitchReportHead = nextHead;
      }
    }
  }
}

void EventDispatcher::applyOutputStates(const byte *coils, size_t coilBytes,
                                        const byte *lamps, size_t lampBytes,
                                        const byte* giLevels) {
  // V2 output frames carry full RAM snapshots. To preserve existing
  // EventListener behavior, we synthesize legacy events only for changed bits
  // (edge detection old snapshot -> new snapshot). This keeps the rest of the
  // firmware event-driven without requiring listener rewrites.
  for (uint16_t n = 0; n < runtimeConfig.coilBits; ++n) {
    bool oldState = ppuc::v2::GetBitmapBit(outputCoils, n);
    bool newState = ppuc::v2::GetBitmapBit(coils, n);
    if (oldState != newState) {
      callListeners(new Event(EVENT_SOURCE_SOLENOID, coilIndexToNumber[n],
                              newState ? 1 : 0),
                    true);
    }
  }
  memcpy(outputCoils, coils, coilBytes);

  for (uint16_t n = 0; n < runtimeConfig.lampBits; ++n) {
    bool oldState = ppuc::v2::GetBitmapBit(outputLamps, n);
    bool newState = ppuc::v2::GetBitmapBit(lamps, n);
    if (oldState != newState) {
      callListeners(new Event(EVENT_SOURCE_LIGHT, lampIndexToNumber[n],
                              newState ? 1 : 0),
                    true);
    }
  }
  memcpy(outputLamps, lamps, lampBytes);

  for (uint8_t giString = 0; giString < ppuc::v2::kGiStrings; ++giString) {
    const uint8_t newLevel = ppuc::v2::ClampGiLevel(
        ppuc::v2::GetPackedNibble(giLevels, giString));
    if (outputGi[giString] != newLevel) {
      callListeners(new Event(EVENT_SOURCE_GI, giString + 1, newLevel), true);
      outputGi[giString] = newLevel;
    }
  }
}

void EventDispatcher::applySwitchStates(const byte* switches,
                                        size_t switchBytes) {
  // Global switch state is board-to-board on the RS485 bus. CPU/libppuc never
  // broadcasts switch states. Every board consumes incoming switch frames and
  // emits local switch events for fast-flip/effect listeners.
  applyingRemoteSwitchState = true;
  for (uint16_t n = 0; n < runtimeConfig.switchBits; ++n) {
    if (ppuc::v2::GetBitmapBit(localOwnedSwitchMask, n)) {
      continue;
    }
    bool oldState = ppuc::v2::GetBitmapBit(switchStates, n);
    bool newState = ppuc::v2::GetBitmapBit(switches, n);
    if (oldState != newState) {
      dispatch(new Event(EVENT_SOURCE_SWITCH, switchIndexToNumber[n],
                         newState ? 1 : 0, true));
    }
  }
  applyingRemoteSwitchState = false;
  for (size_t i = 0; i < switchBytes; ++i) {
    const byte remoteOwnedMask = static_cast<byte>(~localOwnedSwitchMask[i]);
    switchStates[i] = static_cast<byte>((switchStates[i] & localOwnedSwitchMask[i]) |
                                        (switches[i] & remoteOwnedMask));
  }
}

void EventDispatcher::forwardSwitchTokenIfSelected(uint8_t selectedBoard) {
  if (selectedBoard != board) {
    return;
  }
  const bool haveQueuedLocalSnapshots =
      localSwitchReportHead != localSwitchReportTail;

  // Forward the token before running the heavier output/switch fanout logic on
  // core 0. Config ACKs are already fast; runtime replies need the same low
  // latency so switch polling does not depend on lamp/effect processing time.
  if (haveQueuedLocalSnapshots) {
    sendSwitchStateFrame(nextSwitchBoard);
  } else {
    sendSwitchNoChangeFrame(nextSwitchBoard);
  }
}

void EventDispatcher::sendSwitchStateFrame(byte nextBoard) {
  // Switch updates are transmitted as a compact V2 frame containing the full
  // dense switch bitmap. The CPU selects the responding board via token
  // (header.nextBoard in output frame). This board answers once and then
  // returns RS485 direction to RX mode.
  const size_t switchBytes = ppuc::v2::BitsToBytes(runtimeConfig.switchBits);
  const size_t payloadBytes = ppuc::v2::kSwitchStatusBytes + switchBytes;
  const size_t frameBytes =
      ppuc::v2::kHeaderBytes + payloadBytes + ppuc::v2::kCrcBytes;

  byte* frame = v2TxBuffer;
  frame[0] = ppuc::v2::kSyncByte;
  frame[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameSwitchState,
                                           ppuc::v2::kFlagKeyframe);
  frame[2] = nextBoard;
  frame[3] = txSequence++;
  frame[4] = currentEpoch;
  frame[5] = currentEpoch;
  frame[6] = lastHostSequenceSeen;
  frame[7] = currentStatusFlags();
  frame[8] = 0;
  memcpy(&frame[9], switchStates, switchBytes);
  if (localSwitchReportHead != localSwitchReportTail) {
    ApplyMaskedBitmap(&frame[9], localSwitchReportHistory[localSwitchReportTail],
                      localOwnedSwitchMask, switchBytes);
  }

  uint16_t crc =
      ppuc::v2::Crc16Ccitt(frame, ppuc::v2::kHeaderBytes + payloadBytes);
  frame[9 + switchBytes] = highByte(crc);
  frame[10 + switchBytes] = lowByte(crc);

  const uint32_t derivedPostTxSettleUs = switchReplyDelayUs / 4u;
  // Keep the post-TX settle tied to the configured pre-reply delay, but cap it
  // so a large experimental value does not stall core 0 for an excessive time
  // after every reply.
  const uint32_t postTxSettleUs =
      derivedPostTxSettleUs > 2000u ? 2000u : derivedPostTxSettleUs;
  if (switchReplyDelayUs > 0) {
    delayMicroseconds(switchReplyDelayUs);
  }

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, frameBytes);
  delayMicroseconds(FrameWireTimeUs(frameBytes));
  digitalWrite(rs485Pin, LOW);  // Read.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  if (postTxSettleUs > 0) {
    delayMicroseconds(postTxSettleUs);
  }

  v2TxFrames++;
  if (localSwitchReportHead != localSwitchReportTail) {
    memcpy(lastSentLocalReportSwitchStates,
           localSwitchReportHistory[localSwitchReportTail],
           sizeof(lastSentLocalReportSwitchStates));
    localSwitchReportTail = static_cast<uint8_t>(
        (localSwitchReportTail + 1) % SWITCH_REPORT_HISTORY_SIZE);
  }
  clearReportedStatusFlags();
  lastPoll = millis();
}

void EventDispatcher::sendSwitchNoChangeFrame(byte nextBoard) {
  byte* frame = v2TxBuffer;
  frame[0] = ppuc::v2::kSyncByte;
  frame[1] = ppuc::v2::ComposeTypeAndFlags(ppuc::v2::kFrameSwitchNoChange,
                                           ppuc::v2::kFlagNone);
  frame[2] = nextBoard;
  frame[3] = txSequence++;
  frame[4] = currentEpoch;
  frame[5] = currentEpoch;
  frame[6] = lastHostSequenceSeen;
  frame[7] = currentStatusFlags();
  frame[8] = 0;
  uint16_t crc = ppuc::v2::Crc16Ccitt(
      frame, ppuc::v2::kHeaderBytes + ppuc::v2::SwitchNoChangePayloadBytes());
  frame[9] = highByte(crc);
  frame[10] = lowByte(crc);

  const uint32_t derivedPostTxSettleUs = switchReplyDelayUs / 4u;
  // Keep the post-TX settle tied to the configured pre-reply delay, but cap it
  // so a large experimental value does not stall core 0 for an excessive time
  // after every reply.
  const uint32_t postTxSettleUs =
      derivedPostTxSettleUs > 2000u ? 2000u : derivedPostTxSettleUs;
  if (switchReplyDelayUs > 0) {
    delayMicroseconds(switchReplyDelayUs);
  }

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, ppuc::v2::SwitchNoChangeFrameBytes());
  delayMicroseconds(FrameWireTimeUs(ppuc::v2::SwitchNoChangeFrameBytes()));
  digitalWrite(rs485Pin, LOW);  // Read.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  if (postTxSettleUs > 0) {
    delayMicroseconds(postTxSettleUs);
  }

  clearReportedStatusFlags();
  v2TxFrames++;
  v2SwitchNoChangeTx++;
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
  size_t payloadBytes = getV2PayloadBytes(frameType);
  if (frameType != ppuc::v2::kFrameHeartbeat &&
      frameType != ppuc::v2::kFrameError &&
      frameType != ppuc::v2::kFrameReset &&
      frameType != ppuc::v2::kFrameRestart && payloadBytes == 0 &&
      frameType != ppuc::v2::kFrameOutputState &&
      frameType != ppuc::v2::kFrameSwitchNoChange) {
    return false;
  }

  if (!readBytes(&v2Buffer[ppuc::v2::kHeaderBytes],
                 payloadBytes + ppuc::v2::kCrcBytes)) {
    return false;
  }

  return processV2Frame(v2Buffer, payloadBytes);
}

void EventDispatcher::update() {
  if (!rs485) {  // We're on Core1, the EffectController. Transmit stacked
                 // events to Core0.
    while (!eventQueue.empty()) {
      Event *e = eventQueue.front();
      eventQueue.pop();
      callListeners(e, true);
    }
  } else {
    while (!eventQueue.empty()) {
      Event *e = eventQueue.front();
      eventQueue.pop();
      callListeners(e, true);
    }

    if (hwSerial->available() > 0) {
      m_sawRs485Activity = true;
    }
    while (hwSerial->available() > 0) {
      int firstByte = hwSerial->peek();
      if (firstByte >= 0) {
        v2RawBytes++;
        if (firstByte == ppuc::v2::kSyncByte) {
          v2RawA5++;
        } else if (firstByte == 0xFF) {
          v2RawFF++;
        }
      }
      if (firstByte == ppuc::v2::kSyncByte) {
        if (!handleV2Frame()) {
          break;
        }
      } else {
        // Desync/noise, consume one byte and continue.
        parserResynced = true;
        hwSerial->read();
      }
    }
  }

  if (multiCoreCrossLink) {
    // Drain cross-core traffic in bursts instead of one item per loop. Boards
    // with heavy WS2812/effect config can receive well over 100 config frames
    // during startup, and after a soft restart the old one-at-a-time handling
    // can leave core 0 blocked on the queue before board-local config catches
    // up on core 1.
    while (multiCoreCrossLink->eventAvailable()) {
      Event *event = multiCoreCrossLink->popEvent();
      callListeners(event, false);
    }

    while (multiCoreCrossLink->configEventAvailable()) {
      ConfigEvent *configEvent = multiCoreCrossLink->popConfigEvent();
      callListeners(configEvent, false);
    }
  }

  if (debugEnabled && Serial && (millis() - debugLastPrintMs) >= 1000) {
    debugLastPrintMs = millis();
    rp2040.idleOtherCore();
    Serial.print("V2DBG board=");
    Serial.print(board);
    Serial.print(" rx=");
    Serial.print(v2RxFrames);
    Serial.print(" rx_crc_fail=");
    Serial.print(v2RxCrcFail);
    Serial.print(" raw=");
    Serial.print(v2RawBytes);
    Serial.print(" raw_a5=");
    Serial.print(v2RawA5);
    Serial.print(" raw_ff=");
    Serial.print(v2RawFF);
    Serial.print(" tx=");
    Serial.print(v2TxFrames);
    Serial.print(" tx_nochange=");
    Serial.print(v2SwitchNoChangeTx);
    Serial.print(" xcore_drop=");
    Serial.println(crossCoreEventDrops);
    rp2040.resumeOtherCore();
  }
}

uint32_t EventDispatcher::getLastPoll() {
  if (running) return lastPoll;

  return millis();
}

bool EventDispatcher::sawRs485Activity() const { return m_sawRs485Activity; }
