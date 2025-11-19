/*
  IOBoardController.h
  Created by Markus Kalkbrenner.

  GPIO0-7: Input (Switches) or low power output
  GPIO8-15: Input (Switches)
  GPIO16,17,18: UART TX, UART RX, RS485 Direction
  GPIO19-24, 26, 27: Power Out (PWM)
  GPIO25: Status-LED
  GPIO28: ADC for Board ID
  GPIO29: Reserve (z.B. für einen LED-Strip oder zweite Status-LED)
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

  PwmDevices *pwmDevices();

  Switches *switches();

  SwitchMatrix *switchMatrix();

  EventDispatcher *eventDispatcher();

  void handleEvent(Event *event);

  void handleEvent(ConfigEvent *event);

  void update();

  void debug() { m_debug = true; }

 private:
  PwmDevices *_pwmDevices;
  Switches *_switches;
  SwitchMatrix *_switchMatrix;

  bool running = false;
  bool activePwmDevices = false;
  bool m_debug = false;

  int controllerType;
  byte boardId;
  byte port = 0;
  byte number = 0;
  byte power = 0;
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
