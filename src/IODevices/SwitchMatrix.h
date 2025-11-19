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

#define SWITCH_MATRIX_BASE_PIN 0
#define COLUMNS_BASE_PIN (SWITCH_MATRIX_BASE_PIN + 8)
#define MATRIX_SWITCHES 64
#define MATRIX_SWITCH_DEBOUNCE 2

class SwitchMatrix : public EventListener {
 public:
  SwitchMatrix(byte bId, EventDispatcher* eD) {
    boardId = bId;
    _eventDispatcher = eD;
    _eventDispatcher->addListener(this, EVENT_POLL_EVENTS);
    _eventDispatcher->addListener(this, EVENT_READ_SWITCHES);

    pio = pio0;
    sm_columns = 0;
    sm_odd_rows = 2;
    sm_even_rows = 1;
  }

  void setActiveLow();
  void registerSwitch(byte p, byte n);

  void handleEvent(Event* event);

  void handleEvent(ConfigEvent* event) {}

  void handleRowChanges(uint32_t raw, uint8_t even);

  PIO pio;
  int sm_columns;
  int sm_even_rows;
  int sm_odd_rows;

 private:
  byte boardId;
  bool activeLow = false;
  bool running = false;
  bool active = false;

  byte mapping[MATRIX_SWITCHES] = {0};
  uint32_t lastStable[2] = {0};
  absolute_time_t debounceTime[MATRIX_SWITCHES] = {0};

  EventDispatcher* _eventDispatcher;

  static SwitchMatrix* instance;

  static void __not_in_flash_func(onOddRowChanges)() {
    // IRQ0 clear
    pio0_hw->irq = 1u << 0;

    uint32_t raw = pio_sm_get_blocking(instance->pio, instance->sm_odd_rows);
    instance->handleRowChanges(raw, 0);
  }

  static void __not_in_flash_func(onEvenRowChanges)() {
    // IRQ1 clear
    pio0_hw->irq = 1u << 1;

    uint32_t raw = pio_sm_get_blocking(instance->pio, instance->sm_even_rows);
    instance->handleRowChanges(raw, 1);
  }
};

#endif
