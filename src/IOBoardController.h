/*
  IOBoardController.h
  Created by Markus Kalkbrenner.
*/

#ifndef IOBOARDCONTROLLER_h
#define IOBOARDCONTROLLER_h

#include "EventDispatcher/Event.h"
#include "EventDispatcher/EventDispatcher.h"
#include "EventDispatcher/MultiCoreCrossLink.h"
#include "IODevices/PwmDevices.h"
#include "IODevices/SwitchMatrix.h"
#include "IODevices/Switches.h"
#include "PPUC.h"

class IOBoardController : public EventListener {
 public:
  IOBoardController(int controllerType);
  void begin();

  PwmDevices *pwmDevices();

  Switches *switches();

  SwitchMatrix *switchMatrix();

  EventDispatcher *eventDispatcher();

  void handleEvent(Event *event);

  void handleEvent(ConfigEvent *event);

  void update();

  bool isDebug() { return m_debug; }
  bool isInitialized() const { return m_initialized; }

 private:
  int readBoardSelectorRaw() const;
  void initializeBoardIdentity();

  PwmDevices *_pwmDevices;
  Switches *_switches;
  SwitchMatrix *_switchMatrix;

  bool running = false;
  bool activePwmDevices = false;
  bool activeSwitches = false;
  bool activeSwitchMatrix = false;
  bool m_debug = false;
  bool m_initialized = false;

  int controllerType;
  byte boardId;
  byte port = 0;
  byte number = 0;
  byte power = 0;
  byte rows = 0;
  uint16_t minPulseTime = 0;
  uint16_t maxPulseTime = 0;
  byte holdPower = 0;
  uint16_t holdPowerActivationTime = 0;
  byte fastSwitch = 0;
  byte type = 0;
  uint32_t resetTimer = 0;

  EventDispatcher *_eventDispatcher;
  MultiCoreCrossLink *_multiCoreCrossLink;
};

#endif
