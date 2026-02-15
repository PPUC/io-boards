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

class EventDispatcher {
 public:
  EventDispatcher();

  void setRS485ModePin(int pin);

  void setBoard(byte b);

  void setMultiCoreCrossLink(MultiCoreCrossLink* mccl);

  MultiCoreCrossLink* getMultiCoreCrossLink();

  void setCrossLinkSerial(HardwareSerial& reference);

  void addListener(EventListener* eventListener, char sourceId);

  void addListener(EventListener* eventListener);

  void dispatch(Event* event);

  void update();

  uint32_t getLastPoll();

 private:
  bool readBytes(byte* buffer, size_t len);
  bool handleLegacyFrame();
  bool handleV2Frame();
  void sendSwitchStateFrame(byte nextBoard);
  void applyOutputStates(const byte* coils, size_t coilBytes, const byte* lamps,
                         size_t lampBytes);
  void updateSwitchBitmap(Event* event);
  int16_t findMappedIndex(const uint16_t* table, uint16_t count,
                          uint16_t number);

  void callListeners(Event* event, bool sendToOtherCore, bool sendToRS485);

  void callListeners(ConfigEvent* event, bool sendToOtherCore);

  std::queue<Event*> eventQueue;

  EventListener* eventListeners[MAX_EVENT_LISTENERS];
  char eventListenerFilters[MAX_EVENT_LISTENERS];
  int numListeners = -1;

  byte msg[12];
  byte v2Buffer[ppuc::v2::kHeaderBytes + ppuc::v2::kMaxCoilBytes +
                ppuc::v2::kMaxLampBytes + ppuc::v2::kCrcBytes];
  byte outputCoils[ppuc::v2::kMaxCoilBytes] = {0};
  byte outputLamps[ppuc::v2::kMaxLampBytes] = {0};
  byte switchStates[ppuc::v2::kMaxSwitchBytes] = {0};
  uint16_t coilIndexToNumber[ppuc::v2::kMaxCoilBits];
  uint16_t lampIndexToNumber[ppuc::v2::kMaxLampBits];
  uint16_t switchIndexToNumber[ppuc::v2::kMaxSwitchBits];
  byte txSequence = 0;
  ppuc::v2::RuntimeConfig runtimeConfig;

  bool rs485 = false;
  uint8_t rs485Pin = 0;
  byte board = 255;
  bool error = false;
  uint32_t lastPoll;
  bool running = false;

  bool multiCore = false;
  HardwareSerial* hwSerial;
  MultiCoreCrossLink* multiCoreCrossLink;
};

#endif
