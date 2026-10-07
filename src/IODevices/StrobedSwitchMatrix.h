/*
  StrobedSwitchMatrix.h
  Created by Markus Kalkbrenner, 2026.

  The switch matrix of IO_16x8_matrix: eight strobe outputs, sixteen returns.

  Separate from SwitchMatrix on purpose. That class scans a 4-column matrix on
  IO_16_8_1's own inputs with a pair of PIO programs built around four
  consecutive column pins and one 32-bit word per scan. This board has its own
  strobe drivers on GPIOs that are neither consecutive nor ascending, and 128
  positions, so it gets its own PIO program and its own reader rather than a
  third and fourth special case in the other one.

  Play more pinball!
*/

#ifndef StrobedSwitchMatrix_h
#define StrobedSwitchMatrix_h

#include <Arduino.h>

#include "../EventDispatcher/Event.h"
#include "../EventDispatcher/EventDispatcher.h"
#include "../PPUCBoardTypes.h"
#include "PioAllocation.h"
#include "StrobedSwitchMatrixLogic.h"
#include "hardware/pio.h"

#define STROBED_MATRIX_EVENT_QUEUE_SIZE 64

class StrobedSwitchMatrix : public EventListener {
 public:
  StrobedSwitchMatrix(byte bId, EventDispatcher* eD,
                      const ppuc::board::StrobedMatrixProfile& p)
      : boardId(bId), profile(p), _eventDispatcher(eD) {
    logic.configure(profile);
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);
    _eventDispatcher->addListener(this, EVENT_REFRESH_SWITCHES);
  }

  void setActiveLow() { logic.setActiveLow(true); }
  bool setNumRows(uint8_t n) { return logic.setNumRows(n); }
  bool registerSwitch(byte p, byte n);
  void resetConfig();
  bool isActive() const { return active; }

  void handleEvent(Event* event);

  void handleEvent(ConfigEvent* event) {}

  static StrobedSwitchMatrix* instance;

 private:
  static void onSamples();
  void drainSamples();
  void startReader();
  void stopReader();
  void releaseProgram();
  void resendStableStates();

  byte boardId;
  ppuc::board::StrobedMatrixProfile profile;
  StrobedSwitchMatrixLogic logic;
  bool active = false;
  bool running = false;
  bool programLoaded = false;
  PIO pio = nullptr;
  uint sm = 0;
  uint programOffset = 0;

  volatile uint8_t pendingEventHead = 0;
  volatile uint8_t pendingEventTail = 0;
  StrobedSwitchMatrixLogic::Edge
      pendingEvents[STROBED_MATRIX_EVENT_QUEUE_SIZE] = {};
  EventDispatcher* _eventDispatcher;
};

#endif
