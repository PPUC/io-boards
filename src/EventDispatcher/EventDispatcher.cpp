#include "EventDispatcher.h"

#include "../HighPowerOffAware.h"

#include <string.h>

#include "../PPUC.h"        // FIRMWARE_VERSION_*, the single source of truth
                            // that CI also reads to tag releases
#include "hardware/sync.h"  // save_and_disable_interrupts
#include "hardware/uart.h"  // uart0, UART_UARTFR_BUSY_BITS
#include "pico/time.h"      // make_timeout_time_us, time_reached

namespace {
constexpr uint32_t kSerialBaudRate = ppuc::v2::kBaudRate;

uint32_t FrameWireTimeUs(size_t frameBytes) {
  // Approximate 8N1 UART wire time, plus a guard.
  //
  // The guard is proportional, not a flat 200us. readBytes() uses this as a
  // deadline for bytes still in flight, and a fixed guard shrinks to nothing in
  // relative terms as frames grow: a 258 byte firmware chunk takes 22.4ms on
  // the wire and was allowed 23.1ms, a 3% margin that any preemption ate. The
  // symptom was that updates completed with 64 byte chunks and failed at 256.
  const uint32_t bits = static_cast<uint32_t>(frameBytes) * 10;
  const uint32_t wireUs = (bits * 1000000u) / kSerialBaudRate;
  return wireUs + wireUs / 4u + 200;
}

// Pre-reply turnaround for admin frames - version reports and update acks.
//
// Deliberately its own constant rather than switchReplyDelayUs. Both need the
// host's transceiver to have fallen back to receive, but they are paid for
// differently: a switch reply happens on every chain, so every microsecond
// there costs bus throughput, while an admin reply happens at startup or during
// an update and its delay costs nothing measurable. Tying the two together
// meant lowering the switch delay for throughput also made version queries and
// firmware updates less reliable, which is a trade nobody wants to make.
constexpr uint32_t kAdminReplyDelayUs = 2000;

// Extra time allowed on top of a frame's wire time before a partial read is
// abandoned. Covers the sender's DE turnaround (RS485_MODE_SWITCH_DELAY at each
// end) plus loop jitter, with room to spare.
constexpr uint32_t kReadTimeoutSlackUs = 500;

// Releases the RS485 driver as soon as the UART has shifted the last bit out.
//
// This used to wait FrameWireTimeUs() and then drop DE. Because write() only
// has to reach the 32-byte TX FIFO, that estimate expires roughly its 200 us
// guard *after* the frame is actually gone, so every board held the bus 200 us
// longer than necessary - straight into the window where the next board, having
// just received the frame naming it, starts to transmit.
//
// Deliberately not HardwareSerial::flush(). That takes the SerialUART core
// mutex, and CoreMutex either blocks until the other core lets go or, when this
// core already owns it, returns having waited for nothing. The first explains
// the "flush() appears to hang" note this code carried; the second is worse,
// because it would drop DE mid-frame with no sign at all. Waiting on the UART's
// own BUSY flag has neither failure mode.
//
// Serial1 is HW UART 0 (GPIO 0/1, set in main.cpp), so uart0 is the instance
// behind hwSerial.
//
// The wait is bounded: a wedged UART must never stall the board, so on timeout
// it gives up and releases anyway, which is exactly the old behaviour.
void ReleaseBusAfterTx(byte rs485Pin, size_t frameBytes) {
  const absolute_time_t deadline =
      make_timeout_time_us(FrameWireTimeUs(frameBytes));
  while (uart_get_hw(uart0)->fr & UART_UARTFR_BUSY_BITS) {
    if (time_reached(deadline)) {
      break;
    }
    tight_loop_contents();
  }

  // Mask interrupts across the release itself. A dedicated-switch change raises
  // a PIO IRQ, and Switches::onSwitchChanges walks every registered switch; if
  // it lands between the last bit and this write, the board keeps driving the
  // bus for the duration of the handler. Masking cannot prevent an IRQ that has
  // already started, but it stops one from opening this gap.
  const uint32_t irqState = save_and_disable_interrupts();
  digitalWrite(rs485Pin, LOW);  // Read.
  restore_interrupts(irqState);
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
  // Bounded by how long the bytes physically take to arrive, not by a flat
  // constant.
  //
  // This is a busy wait in the middle of the board's main loop: nothing else
  // runs while it spins - not PwmDevices::update(), which is what enforces
  // maxPulseTime, and not the switch event dispatch. So the ceiling here is
  // the board's worst-case blind spot, and it should be no larger than the
  // traffic justifies.
  //
  // Once a sender has started a frame it transmits continuously, so the bytes
  // of one frame arrive back to back. Waiting materially longer than the frame
  // takes on the wire cannot recover a frame that is already lost - it only
  // blocks the board. The margin covers the sender's DE turnaround and loop
  // jitter, and a false sync byte now costs a fraction of a millisecond
  // instead of eight.
  const uint32_t wireUs = FrameWireTimeUs(len);
  const uint32_t timeoutUs = wireUs + kReadTimeoutSlackUs;

  size_t offset = 0;
  uint32_t start = micros();
  while (offset < len) {
    if (hwSerial->available() > 0) {
      buffer[offset++] = hwSerial->read();
      continue;
    }

    if ((micros() - start) > timeoutUs) {
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
    case ppuc::v2::kFrameAdmin:
      return ppuc::v2::kAdminPayloadBytes;
    case ppuc::v2::kFrameSwitchRefresh:
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
  consecutiveSwitchNoChangeReplies = 0;
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
  consecutiveSwitchNoChangeReplies = 0;

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
  uint16_t receivedCrc = ppuc::v2::ReadU16(&frame[crcOffset]);
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
      frameType == ppuc::v2::kFrameSwitchRefresh ||
      frameType == ppuc::v2::kFrameHeartbeat ||
      frameType == ppuc::v2::kFrameError || frameType == ppuc::v2::kFrameReset ||
      frameType == ppuc::v2::kFrameRestart ||
      frameType == ppuc::v2::kFrameAdmin;
  const bool hostPollFrame = frameType == ppuc::v2::kFrameOutputState ||
                             frameType == ppuc::v2::kFrameSwitchRefresh;

  auto noteHostSequence = [&]() {
    if (lastHostFrameSequenceValid &&
        static_cast<uint8_t>(lastHostFrameSequenceSeen + 1) !=
            incomingSequence) {
      sequenceGapDetected = true;
    }
    lastHostFrameSequenceSeen = incomingSequence;
    lastHostFrameSequenceValid = true;
    if (hostPollFrame) {
      lastHostSequenceSeen = incomingSequence;
    }
  };

  if (frameType == ppuc::v2::kFrameSetup) {
    ppuc::v2::RuntimeConfig newConfig;
    ppuc::v2::ReadSetupPayload(&frame[payloadOffset], newConfig);
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
      // A session without mapping frames is complete as soon as it is set up,
      // so the transition announceLocalSwitchStates() waits for in the mapping
      // handler never comes.
      if (mappingComplete) {
        announceLocalSwitchStates();
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
    uint8_t domain;
    uint16_t index;
    uint16_t number;
    ppuc::v2::ReadMappingPayload(&frame[payloadOffset], domain, index, number);

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
    const bool wasComplete = mappingComplete;
    mappingComplete = receivedMappingFrames >= expectedMappingFrames;
    if (mappingComplete && !wasComplete) {
      announceLocalSwitchStates();
    }
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

  if (frameType == ppuc::v2::kFrameSwitchRefresh) {
    if (!runtimeConfigValid || incomingEpoch != currentEpoch) {
      return true;
    }
    forceNextSwitchStateReply = true;
    forwardSwitchTokenIfSelected(frame[2]);
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

  if (frameType == ppuc::v2::kFrameAdmin) {
    uint8_t command = 0;
    uint8_t targetBoard = 0;
    uint8_t data[ppuc::v2::kAdminDataBytes] = {0};
    ppuc::v2::ReadAdminPayload(&frame[payloadOffset], command, targetBoard,
                               data);

    // Answer only when addressed. Administration happens outside the switch
    // chain, so there is no token deciding who may transmit; a broadcast query
    // would put every board on the wire at once.
    if (targetBoard != board) {
      return true;
    }

    // Deliberately not gated on runtimeConfigValid or the epoch. The point of
    // a version query is to work before a session exists - that is when the
    // host needs to know what it is talking to.
    switch (command) {
      case ppuc::v2::kAdminVersionQuery:
        // Counted before replying, so a query that arrives but goes unanswered
        // is distinguishable from one that never arrived. The host cannot tell
        // those apart: both are silence.
        v2VersionQueries++;
        sendVersionReportFrame();
        break;

      case ppuc::v2::kAdminStatsQuery:
        sendStatsReportFrame();
        break;

      case ppuc::v2::kAdminUpdateBegin: {
        // Turn everything off before accepting an image. A board about to
        // reboot into the bootloader must not leave a coil energised, and the
        // host has no way to know what this board was doing.
        dispatch(new Event(EVENT_RESTART));

        uint32_t imageBytes = 0;
        uint16_t imageCrc = 0;
        ppuc::v2::ReadUpdateBegin(&frame[payloadOffset], imageBytes, imageCrc);
        const uint8_t status = firmwareUpdater.begin(imageBytes, imageCrc);
        if (status == ppuc::v2::kUpdateOk) {
          dispatch(new Event(EVENT_FIRMWARE_UPDATE, 1, board));
        }
        sendUpdateAckFrame(ppuc::v2::kAdminUpdateBeginAck, status, 0);
        break;
      }

      case ppuc::v2::kAdminUpdateChunk: {
        uint32_t offset = 0;
        uint16_t length = 0;
        ppuc::v2::ReadUpdateChunkHead(&frame[payloadOffset], offset, length);
        const uint8_t* body = &frame[payloadOffset + ppuc::v2::kAdminPrefixBytes +
                                     ppuc::v2::kUpdateChunkHeadBytes];
        const uint8_t status = firmwareUpdater.chunk(offset, body, length);
        // Acknowledge the offset that was asked about, so a host retrying a
        // lost ack can tell which chunk the answer belongs to.
        sendUpdateAckFrame(ppuc::v2::kAdminUpdateChunkAck, status, offset);
        break;
      }

      case ppuc::v2::kAdminUpdateCommit: {
        const uint8_t status = firmwareUpdater.commit();
        // Either way the transfer is over: a commit reboots into the new image,
        // and a refusal leaves the board running the old one.
        dispatch(new Event(EVENT_FIRMWARE_UPDATE, 0, board));
        sendUpdateAckFrame(ppuc::v2::kAdminUpdateResult, status,
                           firmwareUpdater.bytesReceived());

        if (status == ppuc::v2::kUpdateOk) {
          // Reboot through the existing reset path rather than calling the
          // reboot directly: it gives the effects controller its shutdown
          // window and leaves the reply time to reach the host, and it is the
          // sequence already exercised on every reset.
          dispatch(new Event(EVENT_RESET));
        }
        break;
      }

      default:
        break;
    }
    return true;
  }

  if (frameType == ppuc::v2::kFrameTrigger) {
    if (!runtimeConfigValid || incomingEpoch != currentEpoch) {
      return true;
    }

    uint8_t source;
    uint16_t number;
    uint8_t value;
    ppuc::v2::ReadTriggerPayload(&frame[payloadOffset], source, number, value);
    callListeners(new Event(source, number, value), true);
    return true;
  }

  if (frameType == ppuc::v2::kFrameConfig) {
    uint8_t targetBoard;
    uint8_t topic;
    uint8_t index;
    uint8_t key;
    uint32_t configValue;
    ppuc::v2::ReadConfigPayload(&frame[payloadOffset], targetBoard, topic, index,
                                key, configValue);
    // Remembered here as well as in PwmDevices, because applyOutputStates has
    // to know which coil gates high power before it walks the bitmap.
    if (topic == CONFIG_TOPIC_GAME_ON_SOLENOID && key == CONFIG_TOPIC_NUMBER) {
      gameOnSolenoidNumber = configValue;
    }

    callListeners(new ConfigEvent(targetBoard, topic, index, key, configValue),
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
  ppuc::v2::BuildConfigAckFrame(frame, ppuc::v2::kNoBoard, txSequence++,
                                currentEpoch, boardId, topic, index, key,
                                status);

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, sizeof(frame));
  ReleaseBusAfterTx(rs485Pin, sizeof(frame));
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
}

// Re-announces the state of every switch wired to this board.
//
// Other boards learn a switch's state from this board's reports, and a switch
// only enters the report once its number resolves to a bitmap index - which
// needs the mapping frames. Those arrive last, after setup. The announcement
// made when setup arrives therefore resolved nothing: every switch was looked
// up in the identity map the session starts with, missed, and was left out of
// the report. A switch that then never changed was never reported at all.
//
// On a real machine that was the coin door. It is closed at power-up and stays
// closed, so its own board saw it and every other board went on believing it
// was open - and a board that believes the door is open keeps high power off.
// Three of four boards had no working coils until someone opened and closed
// the door during a game. Before firmware 0.3.0 the door did not gate anything
// and the missing report had no visible effect.
//
// So the announcement is repeated once the numbers resolve, on every session
// including a resync, where the mapping is sent again. Both switch readers
// ignore a second start, and each switch is reported in the state it is
// actually in, so repeating it is harmless.
void EventDispatcher::announceLocalSwitchStates() {
  // Everything recorded before the mapping arrived was recorded against the
  // identity map this session starts with, where position n means number n.
  // A board that reported one of its own switches in that window put it at the
  // position equal to its number and marked that position as its own. Once the
  // real mapping arrives the position usually belongs to a different switch,
  // and the board then ignores that switch from every other board, because it
  // believes it owns it.
  //
  // On a real machine the coin door, switch 200, sat at position 45, and board
  // 4 owned a switch numbered 45. Board 4 ignored the door for the rest of the
  // session and kept high power off, while the boards without such a collision
  // worked. Which board breaks therefore depends on the switch numbering of
  // the game.
  //
  // So the positions are forgotten here and rebuilt by the announcement below,
  // which now runs with numbers that resolve. Remote state is forgotten too:
  // it was recorded under the same wrong map, and the other boards resend it
  // on the next refresh.
  memset(switchStates, 0, sizeof(switchStates));
  memset(localReportSwitchStates, 0, sizeof(localReportSwitchStates));
  memset(localOwnedSwitchMask, 0, sizeof(localOwnedSwitchMask));
  memset(localSwitchReportHistory, 0, sizeof(localSwitchReportHistory));
  localSwitchReportHead = 0;
  localSwitchReportTail = 0;

  dispatch(new Event(EVENT_READ_SWITCHES));
}

void EventDispatcher::refreshDedicatedSwitchState(uint16_t number,
                                                  bool state) {
  const int16_t mappedIndex =
      findMappedIndex(switchIndexToNumber, runtimeConfig.switchBits, number);
  if (mappedIndex < 0) {
    return;
  }

  const uint16_t index = static_cast<uint16_t>(mappedIndex);
  ppuc::v2::SetBitmapBit(switchStates, index, state);
  ppuc::v2::SetBitmapBit(localReportSwitchStates, index, state);
  ppuc::v2::SetBitmapBit(localOwnedSwitchMask, index, true);
}

void EventDispatcher::resetSwitchNoChangeReplies() {
  consecutiveSwitchNoChangeReplies = 0;
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
  // Positions are meaningless until the mapping frames have arrived: the
  // identity map in force until then would file this switch under whatever
  // switch happens to occupy the position matching its number.
  // announceLocalSwitchStates records every local switch once the mapping is
  // complete, so nothing is lost by ignoring these.
  if (!mappingComplete) {
    return;
  }

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

void EventDispatcher::applyOutputStates(const byte *coils, size_t coilBytes,
                                        const byte *lamps, size_t lampBytes,
                                        const byte* giLevels) {
  // V2 output frames carry full RAM snapshots. To preserve existing
  // EventListener behavior, we synthesize legacy events only for changed bits
  // (edge detection old snapshot -> new snapshot). This keeps the rest of the
  // firmware event-driven without requiring listener rewrites.
  // The coil that gates high power goes first, whatever its index.
  //
  // These events are raised in index order, and a coil command that arrives
  // while high power is off is discarded rather than deferred. So a coil whose
  // number sorts below the game-on solenoid was dropped whenever both changed
  // in the same frame - the power arrived two indices too late. A real game
  // never produced that ordering, because PinMAME asserts game-on long before
  // anything fires; a bench coil test produces it every time.
  int16_t gameOnIndex = -1;
  if (gameOnSolenoidNumber > 0) {
    for (uint16_t n = 0; n < runtimeConfig.coilBits; ++n) {
      if (coilIndexToNumber[n] == gameOnSolenoidNumber) {
        gameOnIndex = static_cast<int16_t>(n);
        break;
      }
    }
  }
  if (gameOnIndex >= 0) {
    const uint16_t n = static_cast<uint16_t>(gameOnIndex);
    const bool oldState = ppuc::v2::GetBitmapBit(outputCoils, n);
    const bool newState = ppuc::v2::GetBitmapBit(coils, n);
    if (oldState != newState) {
      callListeners(new Event(EVENT_SOURCE_SOLENOID, coilIndexToNumber[n],
                              newState ? 1 : 0),
                    true);
    }
  }

  for (uint16_t n = 0; n < runtimeConfig.coilBits; ++n) {
    if (static_cast<int16_t>(n) == gameOnIndex) {
      continue;
    }
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
  // Counted the moment this board learns the token names it, before anything
  // that could stop it replying. Against v2TxFrames this separates "never saw
  // the frame selecting me" from "saw it and did not answer" - the two faults
  // are indistinguishable from the host, which only sees silence either way.
  v2Selected++;
  const bool shouldRefreshSwitches = forceNextSwitchStateReply;
  forceNextSwitchStateReply = false;
  const bool haveQueuedLocalSnapshots =
      localSwitchReportHead != localSwitchReportTail;
  const bool shouldForceSwitchState =
      shouldRefreshSwitches ||
      consecutiveSwitchNoChangeReplies >= kMaxConsecutiveSwitchNoChangeReplies;

  // Forward the token before running the heavier output/switch fanout logic on
  // core 0. Config ACKs are already fast; runtime replies need the same low
  // latency so switch polling does not depend on lamp/effect processing time.
  if (shouldRefreshSwitches ||
      consecutiveSwitchNoChangeReplies >= kMaxConsecutiveSwitchNoChangeReplies) {
    dispatch(new Event(EVENT_REFRESH_SWITCHES, 1, 1, true));
  }

  if (haveQueuedLocalSnapshots || shouldForceSwitchState) {
    sendSwitchStateFrame(nextSwitchBoard);
  } else {
    sendSwitchNoChangeFrame(nextSwitchBoard);
    ++consecutiveSwitchNoChangeReplies;
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
  ppuc::v2::WriteHeader(frame, ppuc::v2::kFrameSwitchState,
                        ppuc::v2::kFlagKeyframe, nextBoard, txSequence++,
                        currentEpoch);
  ppuc::v2::WriteSwitchStatus(&frame[ppuc::v2::kHeaderBytes], currentEpoch,
                              lastHostSequenceSeen, currentStatusFlags());
  memcpy(&frame[9], switchStates, switchBytes);
  if (localSwitchReportHead != localSwitchReportTail) {
    ApplyMaskedBitmap(&frame[9], localSwitchReportHistory[localSwitchReportTail],
                      localOwnedSwitchMask, switchBytes);
  }

  ppuc::v2::AppendCrc(frame, ppuc::v2::kHeaderBytes + payloadBytes);

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
  ReleaseBusAfterTx(rs485Pin, frameBytes);
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
  consecutiveSwitchNoChangeReplies = 0;
  clearReportedStatusFlags();
  lastPoll = millis();
}

void EventDispatcher::sendUpdateAckFrame(uint8_t command, uint8_t status,
                                        uint32_t offset) {
  byte* frame = v2TxBuffer;
  const size_t frameBytes = ppuc::v2::BuildUpdateAckFrame(
      frame, command, board, txSequence++, currentEpoch, status, offset);

  // Same turnaround as the version report; see sendVersionReportFrame for why.
  delayMicroseconds(kAdminReplyDelayUs);

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, frameBytes);
  ReleaseBusAfterTx(rs485Pin, frameBytes);
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);

  v2TxFrames++;
}

void EventDispatcher::sendStatsReportFrame() {
  byte* frame = v2TxBuffer;
  const size_t frameBytes = ppuc::v2::BuildStatsReportFrame(
      frame, board, txSequence++, currentEpoch, v2RxFrames, v2RxCrcFail,
      v2RawBytes, v2TxFrames, v2Selected, v2VersionQueries,
      v2VersionReplies,
      g_highPowerGateBits);

  delayMicroseconds(kAdminReplyDelayUs);

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, frameBytes);
  ReleaseBusAfterTx(rs485Pin, frameBytes);
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);

  // Deliberately not counted in v2TxFrames: this frame reports that counter,
  // and incrementing it here would make the number depend on how often it was
  // asked for.
}

void EventDispatcher::sendVersionReportFrame() {
  byte* frame = v2TxBuffer;
  const size_t frameBytes = ppuc::v2::BuildVersionReportFrame(
      frame, board, txSequence++, currentEpoch, FIRMWARE_VERSION_MAJOR,
      FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH,
      ppuc::v2::kAdminCapabilityVersionReport |
          ppuc::v2::kAdminCapabilityFirmwareUpdate,
      PPUC_BOARD_TYPE, PPUC_BUILD_ID);

  // The host drives the bus to send the query and its transceiver needs time to
  // fall back to receive; answering the instant the frame is parsed puts the
  // reply on the wire while the far end is still turning around, and the host
  // sees a clipped frame or nothing.
  delayMicroseconds(kAdminReplyDelayUs);

  digitalWrite(rs485Pin, HIGH);  // Write.
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  hwSerial->write(frame, frameBytes);
  ReleaseBusAfterTx(rs485Pin, frameBytes);
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);

  v2TxFrames++;
  // Counted after the frame has left the shift register, so this is "the board
  // put a version report on the wire" rather than "the board decided to". With
  // v2VersionQueries it says whether a query that arrived was answered.
  v2VersionReplies++;
}

void EventDispatcher::sendSwitchNoChangeFrame(byte nextBoard) {
  byte* frame = v2TxBuffer;
  ppuc::v2::BuildSwitchReplyFrame(
      frame, /*sendState*/ false, nextBoard, txSequence++, currentEpoch,
      currentEpoch, lastHostSequenceSeen, currentStatusFlags(), nullptr, 0);

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
  ReleaseBusAfterTx(rs485Pin, ppuc::v2::SwitchNoChangeFrameBytes());
  delayMicroseconds(RS485_MODE_SWITCH_DELAY);
  if (postTxSettleUs > 0) {
    delayMicroseconds(postTxSettleUs);
  }

  clearReportedStatusFlags();
  v2TxFrames++;
  v2SwitchNoChangeTx++;
  lastPoll = millis();
}

// Reads an admin frame whose header is already in v2Buffer, sizing the body
// from the command rather than assuming the version-report layout.
bool EventDispatcher::handleV2AdminFrame() {
  if (!readBytes(&v2Buffer[ppuc::v2::kHeaderBytes],
                 ppuc::v2::kAdminPrefixBytes)) {
    return false;
  }

  const uint8_t command = v2Buffer[ppuc::v2::kHeaderBytes];
  size_t bodyBytes = 0;
  switch (command) {
    case ppuc::v2::kAdminVersionQuery:
    case ppuc::v2::kAdminVersionReport:
      bodyBytes = ppuc::v2::kAdminDataBytes;
      break;
    case ppuc::v2::kAdminUpdateBegin:
      bodyBytes = ppuc::v2::kUpdateBeginBodyBytes;
      break;
    case ppuc::v2::kAdminUpdateBeginAck:
    case ppuc::v2::kAdminUpdateChunkAck:
    case ppuc::v2::kAdminUpdateResult:
      bodyBytes = ppuc::v2::kUpdateAckBodyBytes;
      break;
    case ppuc::v2::kAdminUpdateCommit:
    case ppuc::v2::kAdminStatsQuery:
      bodyBytes = 0;
      break;
    case ppuc::v2::kAdminStatsReport:
      bodyBytes = ppuc::v2::kStatsBodyBytes;
      break;
    case ppuc::v2::kAdminUpdateChunk: {
      // Two stage: the head carries the length of the data that follows it.
      if (!readBytes(&v2Buffer[ppuc::v2::kHeaderBytes +
                               ppuc::v2::kAdminPrefixBytes],
                     ppuc::v2::kUpdateChunkHeadBytes)) {
        return false;
      }
      const uint16_t length = ppuc::v2::ReadU16(
          &v2Buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kAdminPrefixBytes + 4]);
      if (length > ppuc::v2::kAdminChunkBytes) {
        return false;
      }
      const size_t payloadBytes =
          ppuc::v2::kAdminPrefixBytes + ppuc::v2::kUpdateChunkHeadBytes + length;
      if (!readBytes(&v2Buffer[ppuc::v2::kHeaderBytes +
                               ppuc::v2::kAdminPrefixBytes +
                               ppuc::v2::kUpdateChunkHeadBytes],
                     length + ppuc::v2::kCrcBytes)) {
        return false;
      }
      return processV2Frame(v2Buffer, payloadBytes);
    }
    default:
      // An admin command this firmware does not know. Nothing can be assumed
      // about its length, so the parser resyncs rather than guessing.
      parserResynced = true;
      return false;
  }

  if (!readBytes(&v2Buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kAdminPrefixBytes],
                 bodyBytes + ppuc::v2::kCrcBytes)) {
    return false;
  }

  return processV2Frame(v2Buffer, ppuc::v2::kAdminPrefixBytes + bodyBytes);
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

  // Admin frames are not one size. getV2PayloadBytes() can only answer from the
  // frame type, and for kFrameAdmin it answers kAdminPayloadBytes - the size of
  // a version report. Every other admin frame is a different length, so reading
  // that many bytes for an UpdateBegin waited for six bytes that were never
  // sent, failed CRC on whatever it assembled, and left the parser mid-stream.
  // The board answered nothing, which is why updating over RS485 never worked.
  //
  // The length is a function of the command, and the command is the first
  // payload byte, so the prefix is read first and the rest sized from it.
  if (frameType == ppuc::v2::kFrameAdmin) {
    return handleV2AdminFrame();
  }

  size_t payloadBytes = getV2PayloadBytes(frameType);
  if (frameType != ppuc::v2::kFrameHeartbeat &&
      frameType != ppuc::v2::kFrameError &&
      frameType != ppuc::v2::kFrameReset &&
      frameType != ppuc::v2::kFrameRestart &&
      frameType != ppuc::v2::kFrameSwitchRefresh && payloadBytes == 0 &&
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
