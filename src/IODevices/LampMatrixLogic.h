/*
  LampMatrixLogic.h
  Created by Markus Kalkbrenner, 2026.

  Which outputs a lamp matrix drives in each column slot.

  This is the part of LampMatrix that has nothing to do with the RP2040. It
  turns "lamp 34 is on" into the pin pattern for each column of the strobe, and
  it is where a lamp wired straight to one output is kept apart from the
  matrix lines. Kept free of hardware so it runs in the native tests.

  Play more pinball!
*/

#ifndef LampMatrixLogic_h
#define LampMatrixLogic_h

#include <stdint.h>
#include <string.h>

#include "../PPUCBoardTypes.h"

#ifndef MAX_DIRECT_LAMPS
#define MAX_DIRECT_LAMPS 18
#endif

class LampMatrixLogic {
 public:
  static constexpr uint8_t kMaxColumns = ppuc::board::kMaxLampColumns;
  static constexpr uint8_t kMaxRows = ppuc::board::kMaxLampRows;

  // outBasePin is the GPIO bit 0 of a column word lands on.
  void configure(const ppuc::board::LampMatrixProfile& p, uint32_t pins,
                 uint8_t outBasePin) {
    profile = p;
    lampPins = pins;
    basePin = outBasePin;
    reset();
  }

  void reset() {
    numRows = profile.rows;
    memset(mapping, 0, sizeof(mapping));
    memset(lit, 0, sizeof(lit));
    memset(directPin, 0, sizeof(directPin));
    memset(directNumber, 0, sizeof(directNumber));
    directCount = 0;
    matrixPinsUsed = 0;
    directPinsUsed = 0;
    directLit = 0;
    rebuild();
  }

  // Turns every lamp off but keeps the configuration.
  void allOff() {
    memset(lit, 0, sizeof(lit));
    directLit = 0;
    rebuild();
  }

  // The number of rows a position is counted in: position = column * rows +
  // row.
  bool setNumRows(uint8_t n) {
    if (n == 0 || n > profile.rows || n > kMaxRows) {
      return false;
    }
    numRows = n;
    return true;
  }

  // A lamp at a crossing of the matrix.
  //
  // Refused when either of its lines already carries a lamp wired straight to
  // that output: the strobe would switch that lamp on and off with the column.
  bool registerLamp(uint8_t position, uint8_t number) {
    if (number == 0 || numRows == 0 ||
        position >= static_cast<uint16_t>(profile.columns) * numRows) {
      return false;
    }
    const uint8_t column = static_cast<uint8_t>(position / numRows);
    const uint8_t row = static_cast<uint8_t>(position % numRows);
    const uint32_t lines =
        bitOf(profile.columnPins[column]) | bitOf(profile.rowPins[row]);
    if ((lines & directPinsUsed) != 0) {
      return false;
    }
    mapping[column][row] = number;
    matrixPinsUsed |= lines;
    return true;
  }

  // A lamp between one output and a supply rail: lit for as long as that
  // output is on, whatever the strobe is doing.
  bool registerDirect(uint8_t pin, uint8_t number) {
    if (number == 0 || (lampPins & ppuc::board::pinBit(pin)) == 0 ||
        pin < basePin) {
      return false;
    }
    const uint32_t bit = bitOf(pin);
    if ((bit & matrixPinsUsed) != 0) {
      return false;
    }
    for (uint8_t i = 0; i < directCount; i++) {
      if (directPin[i] == pin) {
        directNumber[i] = number;
        return true;
      }
    }
    if (directCount >= MAX_DIRECT_LAMPS) {
      return false;
    }
    directPin[directCount] = pin;
    directNumber[directCount] = number;
    directCount++;
    directPinsUsed |= bit;
    return true;
  }

  bool hasLamps() const { return matrixPinsUsed != 0 || directPinsUsed != 0; }
  uint8_t rows() const { return numRows; }
  uint8_t columns() const { return profile.columns; }

  // Returns true when the lamp is one of this board's.
  bool setLamp(uint8_t number, bool on) {
    if (number == 0) {
      return false;
    }
    bool found = false;
    for (uint8_t column = 0; column < profile.columns; column++) {
      for (uint8_t row = 0; row < numRows; row++) {
        if (mapping[column][row] != number) {
          continue;
        }
        found = true;
        const uint16_t bit = static_cast<uint16_t>(1u << row);
        lit[column] = static_cast<uint16_t>(on ? (lit[column] | bit)
                                               : (lit[column] & ~bit));
      }
    }
    for (uint8_t i = 0; i < directCount; i++) {
      if (directNumber[i] != number) {
        continue;
      }
      found = true;
      const uint32_t bit = bitOf(directPin[i]);
      directLit = on ? (directLit | bit) : (directLit & ~bit);
    }
    if (found) {
      rebuild();
    }
    return found;
  }

  // The pin pattern for one column slot, bit 0 on outBasePin.
  //
  // A column with nothing lit drives nothing at all - its high-side switch
  // stays off rather than powering a column of dark lamps.
  uint32_t columnWord(uint8_t column) const {
    return column < kMaxColumns ? words[column] : 0;
  }

  // Every GPIO the configured lamps use, as a GPIO mask.
  uint32_t pinsInUse() const {
    return (matrixPinsUsed | directPinsUsed) << basePin;
  }

 private:
  uint32_t bitOf(uint8_t pin) const {
    return pin >= basePin ? (1u << (pin - basePin)) : 0u;
  }

  void rebuild() {
    for (uint8_t column = 0; column < kMaxColumns; column++) {
      uint32_t word = directLit;
      if (column < profile.columns && lit[column] != 0) {
        word |= bitOf(profile.columnPins[column]);
        for (uint8_t row = 0; row < numRows; row++) {
          if (lit[column] & (1u << row)) {
            word |= bitOf(profile.rowPins[row]);
          }
        }
      }
      // One store per word, so the feeder interrupt never sees half of one.
      words[column] = word;
    }
  }

  ppuc::board::LampMatrixProfile profile = {0, 0, {0}, {0}};
  uint32_t lampPins = 0;
  uint8_t basePin = 0;
  uint8_t numRows = 0;
  uint8_t mapping[kMaxColumns][kMaxRows] = {{0}};
  uint16_t lit[kMaxColumns] = {0};
  uint8_t directPin[MAX_DIRECT_LAMPS] = {0};
  uint8_t directNumber[MAX_DIRECT_LAMPS] = {0};
  uint8_t directCount = 0;
  // In word-bit terms, not GPIO terms.
  uint32_t matrixPinsUsed = 0;
  uint32_t directPinsUsed = 0;
  uint32_t directLit = 0;
  volatile uint32_t words[kMaxColumns] = {0};
};

#endif
