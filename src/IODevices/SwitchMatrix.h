/*
  SwitchMatrix_h.
  Created by Markus Kalkbrenner, 2023-2025.
*/

#ifndef SwitchMatrix_h
#define SwitchMatrix_h

#include "../EventDispatcher/Event.h"
#include "../EventDispatcher/EventDispatcher.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"

#define SWITCH_MATRIX_MAX_COLUMNS 10
#define SWITCH_MATRIX_MAX_ROWS 8
#define MATRIX_SWITCH_DEBOUNCE 2
#define MATRIX_SWITCH_EVENT_QUEUE_SIZE 32

struct SwitchMatrixProfile {
  uint8_t columns;
  uint8_t maxRows;
  uint8_t columnsBasePin;
  uint16_t supportedRowsMask;
};

struct PendingMatrixSwitchEvent {
  uint8_t number;
  uint8_t state;
};

class SwitchMatrix : public EventListener {
 public:
  SwitchMatrix(byte bId, EventDispatcher* eD, const SwitchMatrixProfile& p) {
    boardId = bId;
    _eventDispatcher = eD;
    profile = p;
    numRows = profile.maxRows >= 4 ? 4 : profile.maxRows;
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);
    _eventDispatcher->addListener(this, EVENT_REFRESH_SWITCHES);
  }

  void setActiveLow() { activeLow = true; }
  bool setNumRows(uint8_t n);
  void registerSwitch(byte p, byte n);
  void resetConfig();
  uint8_t columns() const { return profile.columns; }
  uint8_t maxRows() const { return profile.maxRows; }
  uint8_t matrixPinsUsed() const { return profile.columns + numRows; }
  bool supportsRows(uint8_t n) const;

  void handleEvent(Event* event);

  void handleEvent(ConfigEvent* event) {}

  void handleRowChanges(uint32_t raw);

  PIO pio = pio0;
  int sm_columns = 0;
  int sm_rows = 1;

  static SwitchMatrix* instance;

  static void __not_in_flash_func(onRowChanges)() {
    // IRQ0 clear
    pio0_hw->irq = 1u << 0;

    if (!instance ||
        pio_sm_is_rx_fifo_empty(instance->pio, instance->sm_rows)) {
      return;
    }
    uint32_t raw = pio_sm_get_blocking(instance->pio, instance->sm_rows);
    instance->handleRowChanges(raw);
  }

  private:
  void stopReader();
  void startReader();
  void resendStableStates();
  bool isConfiguredGeometrySupported() const;
  byte boardId;
  SwitchMatrixProfile profile = {0, 0, 0, 0};
  bool activeLow = false;
  uint8_t numRows = 4;
  bool running = false;
  bool active = false;
  bool columnsProgramLoaded = false;
  bool rowsProgramLoaded = false;
  uint columnsProgramOffset = 0;
  uint rowsProgramOffset = 0;
  bool loadedActiveLow = false;
  uint8_t loadedNumRows = 4;

  byte mapping[SWITCH_MATRIX_MAX_COLUMNS * SWITCH_MATRIX_MAX_ROWS] = {0};
  uint32_t lastStable = 0;
  volatile uint8_t pendingEventHead = 0;
  volatile uint8_t pendingEventTail = 0;
  PendingMatrixSwitchEvent pendingEvents[MATRIX_SWITCH_EVENT_QUEUE_SIZE] = {};
  absolute_time_t debounceTime[SWITCH_MATRIX_MAX_COLUMNS * SWITCH_MATRIX_MAX_ROWS][2] = {0};
  EventDispatcher* _eventDispatcher;
};

#endif
