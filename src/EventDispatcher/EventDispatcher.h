/*
  EventDispatcher.h
  Created by Markus Kalkbrenner, 2020-2021.

  Play more pinball!
*/

#ifndef EVENTDISPATCHER_h
#define EVENTDISPATCHER_h

#include <Arduino.h>

#include "Event.h"
#include "EventListener.h"
#include "MultiCoreCrossLink.h"

#ifndef MAX_EVENT_LISTENERS
#define MAX_EVENT_LISTENERS 32
#endif

#ifndef MAX_CROSS_LINKS
#define MAX_CROSS_LINKS 8
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

  void addCrossLinkSerial(HardwareSerial& reference);

  void addListener(EventListener* eventListener, char sourceId);

  void addListener(EventListener* eventListener);

  void dispatch(Event* event);

  void update();

  uint32_t getLastPoll();

 private:
  void callListeners(Event* event, int sender, bool flush);

  void callListeners(ConfigEvent* event, int sender);

  Event* stackEvents[EVENT_STACK_SIZE];
  int stackCounter = -1;

  EventListener* eventListeners[MAX_EVENT_LISTENERS];
  char eventListenerFilters[MAX_EVENT_LISTENERS];
  int numListeners = -1;

  byte msg[12];

  bool rs485 = false;
  int rs485Pin = 0;
  byte board = 255;
  bool error = false;
  uint32_t lastPoll;
  bool running = false;

  bool multiCore = false;
  int crossLink = -1;
  HardwareSerial* hwSerial[MAX_CROSS_LINKS];
  MultiCoreCrossLink* multiCoreCrossLink;
};

#endif
