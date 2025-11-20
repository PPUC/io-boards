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

#define COLUMNS_BASE_PIN 15 // GPIO 15-18 for columns, the only pins with required hardware on IO_16_8_1 board
#define NUM_COLUMNS 4
#define MAX_ROWS 8
#define MATRIX_SWITCH_DEBOUNCE 2

class SwitchMatrix : public EventListener {
 public:
  SwitchMatrix(byte bId, EventDispatcher* eD) {
    boardId = bId;
    _eventDispatcher = eD;
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);
  }

  void setActiveLow() { activeLow = true; }
  void setNumRows(uint8_t n) { numRows = n; }
  void registerSwitch(byte p, byte n);

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

    uint32_t raw = pio_sm_get_blocking(instance->pio, instance->sm_rows);
    instance->handleRowChanges(raw);
  }

  private:
  byte boardId;
  bool activeLow = false;
  uint8_t numRows = 4;
  bool running = false;
  bool active = false;

  byte mapping[NUM_COLUMNS * MAX_ROWS] = {0};
  uint32_t lastStable = 0;
  absolute_time_t debounceTime[NUM_COLUMNS * MAX_ROWS][2] = {0};
  EventDispatcher* _eventDispatcher;
};

#endif
