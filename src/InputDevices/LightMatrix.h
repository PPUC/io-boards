/*
  LightMatrix.h
  Created by Markus Kalkbrenner, 2020.
  Derived from afterglow: 2018-2019 Christoph Schmid

  Play more pinball!
*/

#ifndef LightMatrix_h
#define LightMatrix_h

#include <Arduino.h>
#if defined(__AVR__) || (__avr__) || (__IMXRT1062__)
#include <TimerOne.h>
#endif

#include "../EventDispatcher/Event.h"
#include "Matrix.h"

// Number of consistent data samples required for matrix update
#define SINGLE_UPDATE_CONS 2

// local time interval, config A [us]
#define TTAG_INT_A (250)

class LightMatrix : public Matrix {
 public:
  LightMatrix(EventDispatcher* eD, byte pf) : Matrix(eD, pf) {
    lightMatrixInstance = this;

    eventSource = EVENT_SOURCE_LIGHT;
    maxChangesPerRead = MAX_FIELDS_REGISTERED;

    pinMode(5, INPUT);
    pinMode(6, OUTPUT);
    pinMode(7, OUTPUT);

#if defined(__AVR__) || (__avr__) || (__IMXRT1062__)
    Timer1.initialize(TTAG_INT_A);
#endif
  }

  void start();

  void stop();

  static void _readRow();

  uint16_t sampleInput();

  bool updateValid(byte inCol, byte inRowMask);

  void updateCol(uint32_t col, byte rowMask);

  // remember the last row samples
  volatile byte lastRowMask[8] = {0};
  volatile byte consistentSamples[8] = {0};

 private:
  static LightMatrix* lightMatrixInstance;
};

#endif
