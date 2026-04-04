#include "EventDispatcher.h"

#include "hardware/uart.h"
#include <string.h>

namespace {
constexpr uint32_t kV2RxTimeoutUs = 8000;
constexpr bool kEnableV2UartDmaRx = false;
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

void EventDispatcher::callListeners(Event *event, bool sendToOtherCore) {
  if (!event->localFast) {
    for (byte i = 0; i <= numListeners; i++) {
      if (event->sourceId == eventListenerFilters[i] ||
          EVENT_SOURCE_ANY == eventListenerFilters[i]) {
        eventListeners[i]->handleEvent(event);
      }
    }
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
    case ppuc::v2::kFrameHeartbeat:
    case ppuc::v2::kFrameError:
    case ppuc::v2::kFrameReset:
      return 0;
    default:
      return 0;
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
  lastHostSequenceValid = false;
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
      frameType == ppuc::v2::kFrameOutputState ||
      frameType == ppuc::v2::kFrameHeartbeat ||
      frameType == ppuc::v2::kFrameError || frameType == ppuc::v2::kFrameReset;
  const bool hostOutputFrame = frameType == ppuc::v2::kFrameOutputState;

  auto noteHostSequence = [&]() {
    if (!hostOutputFrame) {
      return;
    }
    if (lastHostSequenceValid &&
        static_cast<uint8_t>(lastHostSequenceSeen + 1) != incomingSequence) {
      sequenceGapDetected = true;
    }
    lastHostSequenceSeen = incomingSequence;
    lastHostSequenceValid = true;
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
      if (!v2UartDmaActive) {
        if (startV2UartDmaTransport()) {
          v2CutoverOk++;
        } else {
          v2CutoverFail++;
        }
      }
    }
    return true;
  }

  if (frameType == ppuc::v2::kFrameReset) {
    runtimeConfigValid = false;
    mappingComplete = false;
    expectedMappingFrames = 0;
    receivedMappingFrames = 0;
    v2RuntimeInitialized = false;
    lastHostSequenceValid = false;
    sequenceGapDetected = false;
    parserResynced = false;
    noteHostSequence();
    dispatch(new Event(EVENT_RESET));
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
    const size_t coilBytes = ppuc::v2::BitsToBytes(runtimeConfig.coilBits);
    const size_t lampBytes = ppuc::v2::BitsToBytes(runtimeConfig.lampBits);
    applyOutputStates(&frame[payloadOffset], coilBytes,
                      &frame[payloadOffset + coilBytes], lampBytes,
                      &frame[payloadOffset + coilBytes + lampBytes]);
    if (frame[2] == board) {
      if (switchDirty) {
        sendSwitchStateFrame(nextSwitchBoard);
        switchDirty = false;
      } else {
        sendSwitchNoChangeFrame(nextSwitchBoard);
      }
    }
    return true;
  }

  if (frameType == ppuc::v2::kFrameSwitchState) {
    if (!runtimeConfigValid || incomingEpoch != currentEpoch) {
      return true;
    }
    const size_t switchBytes = ppuc::v2::BitsToBytes(runtimeConfig.switchBits);
    applySwitchStates(&frame[payloadOffset + ppuc::v2::kSwitchStatusBytes],
                      switchBytes);
    if (frame[2] == board) {
      if (switchDirty) {
        sendSwitchStateFrame(nextSwitchBoard);
        switchDirty = false;
      } else {
        sendSwitchNoChangeFrame(nextSwitchBoard);
      }
    }
    return true;
  }

  if (frameType == ppuc::v2::kFrameSwitchNoChange) {
    if (runtimeConfigValid && incomingEpoch == currentEpoch && frame[2] == board) {
      if (switchDirty) {
        sendSwitchStateFrame(nextSwitchBoard);
        switchDirty = false;
      } else {
        sendSwitchNoChangeFrame(nextSwitchBoard);
      }
    }
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

  if (!v2UartDmaActive || !sendV2FrameUartDma(frame, sizeof(frame))) {
    digitalWrite(rs485Pin, HIGH);  // Write.
    delayMicroseconds(RS485_MODE_SWITCH_DELAY);
    hwSerial->write(frame, sizeof(frame));
    delayMicroseconds(FrameWireTimeUs(sizeof(frame)));
    digitalWrite(rs485Pin, LOW);  // Read.
    delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  }
}

bool EventDispatcher::startV2UartDmaTransport() {
  if (!kEnableV2UartDmaRx) {
    return false;
  }

  if (v2UartDmaActive) {
    return true;
  }

  int rxDma = dma_claim_unused_channel(false);
  if (rxDma < 0) {
    return false;
  }

  int txDma = dma_claim_unused_channel(false);
  if (txDma < 0) {
    dma_channel_unclaim(rxDma);
    return false;
  }
  v2RxDmaChannel = rxDma;
  v2TxDmaChannel = txDma;

  v2UartDmaActive = true;
  v2RxState = V2_RX_IDLE;
  v2RxPayloadBytes = 0;
  v2RxStateStartUs = micros();

  return true;
}

void EventDispatcher::stopV2UartDmaTransport() {
  if (!v2UartDmaActive) {
    return;
  }

  dma_channel_abort(v2RxDmaChannel);
  dma_channel_abort(v2TxDmaChannel);
  dma_channel_unclaim(v2RxDmaChannel);
  dma_channel_unclaim(v2TxDmaChannel);

  v2UartDmaActive = false;
  v2RxState = V2_RX_IDLE;
  v2RxDmaChannel = -1;
  v2TxDmaChannel = -1;
}

bool EventDispatcher::sendV2FrameUartDma(const byte* frame, size_t frameBytes) {
  if (!v2UartDmaActive || !frame || frameBytes == 0) {
    return false;
  }

  memcpy(v2DmaTxBuffer, frame, frameBytes);
  dma_channel_config txConfig = dma_channel_get_default_config(v2TxDmaChannel);
  channel_config_set_transfer_data_size(&txConfig, DMA_SIZE_8);
  channel_config_set_dreq(&txConfig, uart_get_dreq(uart1, true));
  channel_config_set_read_increment(&txConfig, true);
  channel_config_set_write_increment(&txConfig, false);

  const uint32_t derivedPostTxSettleUs = switchReplyDelayUs / 4u;
  // Keep the post-TX settle tied to the configured pre-reply delay, but cap it
  // so a large experimental value does not stall core 0 for an excessive time
  // after every reply.
  const uint32_t postTxSettleUs =
      derivedPostTxSettleUs > 2000u ? 2000u : derivedPostTxSettleUs;
  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  dma_channel_configure(v2TxDmaChannel, &txConfig, &uart1_hw->dr,
                        v2DmaTxBuffer, frameBytes, true);
  dma_channel_wait_for_finish_blocking(v2TxDmaChannel);
  uart_tx_wait_blocking(uart1);
  digitalWrite(rs485Pin, LOW);  // Read.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  if (postTxSettleUs > 0) {
    delayMicroseconds(postTxSettleUs);
  }
  v2TxFrames++;
  return true;
}

void EventDispatcher::serviceV2UartDmaRx() {
  if (!v2UartDmaActive) {
    return;
  }

  if (v2RxState == V2_RX_IDLE) {
    dma_channel_config rxConfig = dma_channel_get_default_config(v2RxDmaChannel);
    channel_config_set_transfer_data_size(&rxConfig, DMA_SIZE_8);
    channel_config_set_dreq(&rxConfig, uart_get_dreq(uart1, false));
    channel_config_set_read_increment(&rxConfig, false);
    channel_config_set_write_increment(&rxConfig, true);
    dma_channel_configure(v2RxDmaChannel, &rxConfig, v2DmaRxBuffer,
                          &uart1_hw->dr, ppuc::v2::kHeaderBytes, true);
    v2RxState = V2_RX_HEADER;
    return;
  }

  if (v2RxState == V2_RX_HEADER && dma_channel_is_busy(v2RxDmaChannel)) {
    return;
  }

  if (v2RxState == V2_RX_HEADER) {
    if (v2DmaRxBuffer[0] != ppuc::v2::kSyncByte) {
      v2RxSyncFail++;
      parserResynced = true;
      v2RxState = V2_RX_IDLE;
      return;
    }
    ppuc::v2::FrameType frameType = ppuc::v2::ExtractType(v2DmaRxBuffer[1]);
    v2RxPayloadBytes = getV2PayloadBytes(frameType);

    dma_channel_config rxConfig = dma_channel_get_default_config(v2RxDmaChannel);
    channel_config_set_transfer_data_size(&rxConfig, DMA_SIZE_8);
    channel_config_set_dreq(&rxConfig, uart_get_dreq(uart1, false));
    channel_config_set_read_increment(&rxConfig, false);
    channel_config_set_write_increment(&rxConfig, true);
    dma_channel_configure(v2RxDmaChannel, &rxConfig,
                          &v2DmaRxBuffer[ppuc::v2::kHeaderBytes],
                          &uart1_hw->dr,
                          v2RxPayloadBytes + ppuc::v2::kCrcBytes, true);
    v2RxState = V2_RX_BODY;
    v2RxStateStartUs = micros();
    return;
  }

  if (v2RxState == V2_RX_BODY && dma_channel_is_busy(v2RxDmaChannel)) {
    if ((micros() - v2RxStateStartUs) > kV2RxTimeoutUs) {
      dma_channel_abort(v2RxDmaChannel);
      v2RxState = V2_RX_IDLE;
      v2RxDmaTimeouts++;
    }
    return;
  }

  if (v2RxState == V2_RX_BODY) {
    processV2Frame(v2DmaRxBuffer, v2RxPayloadBytes);
    v2RxState = V2_RX_IDLE;
  }
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

  ppuc::v2::SetBitmapBit(switchStates, (uint16_t)mappedIndex,
                         event->value != 0);
  if (!applyingRemoteSwitchState) {
    switchDirty = true;
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
    bool oldState = ppuc::v2::GetBitmapBit(switchStates, n);
    bool newState = ppuc::v2::GetBitmapBit(switches, n);
    if (oldState != newState) {
      dispatch(new Event(EVENT_SOURCE_SWITCH, switchIndexToNumber[n],
                         newState ? 1 : 0, true));
    }
  }
  applyingRemoteSwitchState = false;
  memcpy(switchStates, switches, switchBytes);
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

  byte* frame = v2DmaTxBuffer;
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

  if (!v2UartDmaActive || !sendV2FrameUartDma(frame, frameBytes)) {
    v2TxFallback++;
    digitalWrite(rs485Pin, HIGH);  // Write.
    delayMicroseconds(RS485_MODE_SWITCH_DELAY);
    hwSerial->write(frame, frameBytes);
    delayMicroseconds(FrameWireTimeUs(frameBytes));
    digitalWrite(rs485Pin, LOW);  // Read.
    delayMicroseconds(RS485_MODE_SWITCH_DELAY);
    if (postTxSettleUs > 0) {
      delayMicroseconds(postTxSettleUs);
    }
  }

  clearReportedStatusFlags();
  lastPoll = millis();
}

void EventDispatcher::sendSwitchNoChangeFrame(byte nextBoard) {
  byte* frame = v2DmaTxBuffer;
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

  if (!v2UartDmaActive ||
      !sendV2FrameUartDma(frame, ppuc::v2::SwitchNoChangeFrameBytes())) {
    v2TxFallback++;
    digitalWrite(rs485Pin, HIGH);  // Write.
    delayMicroseconds(RS485_MODE_SWITCH_DELAY);
    hwSerial->write(frame, ppuc::v2::SwitchNoChangeFrameBytes());
    delayMicroseconds(
        FrameWireTimeUs(ppuc::v2::SwitchNoChangeFrameBytes()));
    digitalWrite(rs485Pin, LOW);  // Read.
    delayMicroseconds(RS485_MODE_SWITCH_DELAY);
    if (postTxSettleUs > 0) {
      delayMicroseconds(postTxSettleUs);
    }
  }

  clearReportedStatusFlags();
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
      frameType != ppuc::v2::kFrameReset && payloadBytes == 0 &&
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

    if (v2UartDmaActive) {
      serviceV2UartDmaRx();
    } else {
      if (hwSerial->available() > 0) {
        m_sawRs485Activity = true;
      }
      // Fallback parser is still needed for V2 bootstrap and fault handling:
      // - bootstrap: receive initial V2 setup frame before DMA cutover
      // - fault path: continue operating if UART DMA transport cannot start
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
  }

  if (multiCoreCrossLink) {
    if (multiCoreCrossLink->eventAvailable()) {
      Event *event = multiCoreCrossLink->popEvent();
      callListeners(event, false);
    }

    if (multiCoreCrossLink->configEventAvailable()) {
      ConfigEvent *configEvent = multiCoreCrossLink->popConfigEvent();
      callListeners(configEvent, false);
    }
  }

  if (debugEnabled && Serial && (millis() - debugLastPrintMs) >= 1000) {
    debugLastPrintMs = millis();
    rp2040.idleOtherCore();
    Serial.print("V2DBG board=");
    Serial.print(board);
    Serial.print(" active=");
    Serial.print(v2UartDmaActive ? 1 : 0);
    Serial.print(" cutover_ok=");
    Serial.print(v2CutoverOk);
    Serial.print(" cutover_fail=");
    Serial.print(v2CutoverFail);
    Serial.print(" rx=");
    Serial.print(v2RxFrames);
    Serial.print(" rx_crc_fail=");
    Serial.print(v2RxCrcFail);
    Serial.print(" rx_sync_fail=");
    Serial.print(v2RxSyncFail);
    Serial.print(" rx_dma_restart=");
    Serial.print(v2RxDmaRestarts);
    Serial.print(" rx_dma_timeout=");
    Serial.print(v2RxDmaTimeouts);
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
    Serial.print(" tx_fallback=");
    Serial.println(v2TxFallback);
    rp2040.resumeOtherCore();
  }
}

uint32_t EventDispatcher::getLastPoll() {
  if (running) return lastPoll;

  return millis();
}

bool EventDispatcher::sawRs485Activity() const { return m_sawRs485Activity; }
