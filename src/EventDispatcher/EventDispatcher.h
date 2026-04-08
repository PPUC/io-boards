/*
  EventDispatcher.h
  Created by Markus Kalkbrenner, 2020-2021.

  Play more pinball!
*/

#ifndef EVENTDISPATCHER_h
#define EVENTDISPATCHER_h

#include <Arduino.h>

#include <queue>

#include "Event.h"
#include "EventListener.h"
#include "MultiCoreCrossLink.h"
#include "../PPUCProtocolV2.h"

#ifndef MAX_EVENT_LISTENERS
#define MAX_EVENT_LISTENERS 32
#endif

#ifndef EVENT_STACK_SIZE
#define EVENT_STACK_SIZE 100
#endif

#ifndef SWITCH_REPORT_HISTORY_SIZE
#define SWITCH_REPORT_HISTORY_SIZE 32
#endif

class EventDispatcher {
 public:
  EventDispatcher();

  void setRS485ModePin(int pin);

  void setBoard(byte b);

  void setMultiCoreCrossLink(MultiCoreCrossLink* mccl);

  MultiCoreCrossLink* getMultiCoreCrossLink();

  void setCrossLinkSerial(HardwareSerial& reference);
  void setDebug(bool enabled);
  void setNextSwitchBoard(byte boardId);
  void setSwitchReplyDelayUs(uint32_t delayUs);

  void addListener(EventListener* eventListener, char sourceId);

  void addListener(EventListener* eventListener);
  void removeListener(EventListener* eventListener);
  bool getSwitchState(uint16_t number) const;

  void dispatch(Event* event);

  void update();

  uint32_t getLastPoll();
  bool sawRs485Activity() const;

 private:
  bool readBytes(byte* buffer, size_t len);
  bool handleV2Frame();
  size_t getV2PayloadBytes(ppuc::v2::FrameType frameType);
  bool processV2Frame(const byte* frame, size_t payloadBytes);
  void sendConfigAckFrame(uint8_t boardId, uint8_t topic, uint8_t index,
                          uint8_t key, uint8_t status);
  void sendSwitchStateFrame(byte nextBoard);
  void sendSwitchNoChangeFrame(byte nextBoard);
  void forwardSwitchTokenIfSelected(uint8_t selectedBoard);
  void applyOutputStates(const byte* coils, size_t coilBytes, const byte* lamps,
                         size_t lampBytes, const byte* giLevels);
  void applySwitchStates(const byte* switches, size_t switchBytes);
  void updateSwitchBitmap(Event* event);
  void clearSessionState();
  void resetSessionState(uint8_t newEpoch, const ppuc::v2::RuntimeConfig& cfg);
  uint8_t currentStatusFlags() const;
  void clearReportedStatusFlags();
  int16_t findMappedIndex(const uint16_t* table, uint16_t count,
                          uint16_t number) const;
  bool shouldDropOnCrossCoreBackpressure(const Event* event) const;

  void callListeners(Event* event, bool sendToOtherCore);

  void callListeners(ConfigEvent* event, bool sendToOtherCore);

  std::queue<Event*> eventQueue;

  EventListener* eventListeners[MAX_EVENT_LISTENERS];
  char eventListenerFilters[MAX_EVENT_LISTENERS];
  int numListeners = -1;

  byte v2Buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kMaxCoilBytes +
                ppuc::v2::kMaxLampBytes + ppuc::v2::kGiBytes +
                ppuc::v2::kCrcBytes];
  byte v2TxBuffer[ppuc::v2::kHeaderBytes + ppuc::v2::kSwitchStatusBytes +
                  ppuc::v2::kMaxSwitchBytes + ppuc::v2::kCrcBytes];
  byte outputCoils[ppuc::v2::kMaxCoilBytes] = {0};
  byte outputLamps[ppuc::v2::kMaxLampBytes] = {0};
  byte outputGi[ppuc::v2::kGiStrings] = {0};
  byte switchStates[ppuc::v2::kMaxSwitchBytes] = {0};
  byte localReportSwitchStates[ppuc::v2::kMaxSwitchBytes] = {0};
  byte localOwnedSwitchMask[ppuc::v2::kMaxSwitchBytes] = {0};
  byte localSwitchReportHistory[SWITCH_REPORT_HISTORY_SIZE]
                               [ppuc::v2::kMaxSwitchBytes] = {{0}};
  uint8_t localSwitchReportHead = 0;
  uint8_t localSwitchReportTail = 0;
  uint16_t coilIndexToNumber[ppuc::v2::kMaxCoilBits];
  uint16_t lampIndexToNumber[ppuc::v2::kMaxLampBits];
  uint16_t switchIndexToNumber[ppuc::v2::kMaxSwitchBits];
  byte txSequence = 0;
  ppuc::v2::RuntimeConfig runtimeConfig;
  bool debugEnabled = false;
  uint32_t debugLastPrintMs = 0;
  uint32_t v2RxFrames = 0;
  uint32_t v2RxCrcFail = 0;
  uint32_t v2RawBytes = 0;
  uint32_t v2RawA5 = 0;
  uint32_t v2RawFF = 0;
  bool m_sawRs485Activity = false;
  uint32_t v2TxFrames = 0;
  uint32_t v2SwitchNoChangeTx = 0;
  uint32_t crossCoreEventDrops = 0;
  bool switchOverflow = false;
  bool applyingRemoteSwitchState = false;
  bool v2RuntimeInitialized = false;
  bool runtimeConfigValid = false;
  bool mappingComplete = false;
  uint16_t expectedMappingFrames = 0;
  uint16_t receivedMappingFrames = 0;
  uint8_t currentEpoch = 0;
  uint8_t lastHostSequenceSeen = 0;
  bool lastHostSequenceValid = false;
  bool sequenceGapDetected = false;
  bool parserResynced = false;
  bool transportErrorLatched = false;

  bool rs485 = false;
  uint8_t rs485Pin = 0;
  byte board = 255;
  byte nextSwitchBoard = ppuc::v2::kNoBoard;
  uint32_t switchReplyDelayUs = 0;
  uint32_t lastPoll;
  bool running = false;

  bool multiCore = false;
  HardwareSerial* hwSerial;
  MultiCoreCrossLink* multiCoreCrossLink;
};

#endif
