/*
  StrobedSwitchMatrixLogic.h
  Created by Markus Kalkbrenner, 2026.

  Turns the samples of a strobed switch matrix into switch edges.

  This is the part of StrobedSwitchMatrix that has nothing to do with the
  RP2040: which strobe a sample belongs to, what the sixteen return bits mean,
  and when a change has lasted long enough to report. Kept apart from the PIO
  setup so it runs in the native tests, where a mistake in the column order
  costs a failed assertion instead of a playfield where every switch reports
  as its neighbour.

  Play more pinball!
*/

#ifndef StrobedSwitchMatrixLogic_h
#define StrobedSwitchMatrixLogic_h

#include <stdint.h>
#include <string.h>

#include "../PPUCBoardTypes.h"

// How long a switch must hold a new state, measured from the last edge
// reported in that direction, before the next one is reported. The same
// lockout SwitchMatrix applies.
#define STROBED_MATRIX_DEBOUNCE_US 2000

class StrobedSwitchMatrixLogic {
 public:
  static constexpr uint8_t kMaxColumns = ppuc::board::kMaxStrobes;
  static constexpr uint8_t kMaxRows = 16;
  static constexpr uint8_t kNoColumn = 0xFF;

  struct Edge {
    uint8_t number;
    uint8_t state;
  };

  void configure(const ppuc::board::StrobedMatrixProfile& p) {
    profile = p;
    reset();
  }

  // Forgets every switch and goes back to the defaults a board has before the
  // host configures it.
  void reset() {
    activeLow = false;
    numRows = profile.maxReturns;
    memset(mapping, 0, sizeof(mapping));
    memset(candidate, 0, sizeof(candidate));
    memset(haveCandidate, 0, sizeof(haveCandidate));
    memset(stable, 0, sizeof(stable));
    memset(lastEdgeUs, 0, sizeof(lastEdgeUs));
    registered = false;
  }

  // Forgets what was sampled but keeps the configuration, so the next scan
  // starts from "every switch open" and reports whatever is closed.
  void forgetSamples() {
    memset(candidate, 0, sizeof(candidate));
    memset(haveCandidate, 0, sizeof(haveCandidate));
    memset(stable, 0, sizeof(stable));
    memset(lastEdgeUs, 0, sizeof(lastEdgeUs));
  }

  void setActiveLow(bool low) { activeLow = low; }

  // The number of returns a position is counted in: position = column * rows +
  // row. All sixteen returns are sampled whatever this is set to.
  bool setNumRows(uint8_t n) {
    if (n == 0 || n > profile.maxReturns || n > kMaxRows) {
      return false;
    }
    numRows = n;
    return true;
  }

  bool registerSwitch(uint8_t position, uint8_t number) {
    if (number == 0 || numRows == 0 ||
        position >= static_cast<uint16_t>(profile.strobes) * numRows) {
      return false;
    }
    mapping[position] = number;
    registered = true;
    return true;
  }

  bool hasSwitches() const { return registered; }
  uint8_t rows() const { return numRows; }
  uint8_t columns() const { return profile.strobes; }

  // The column a strobe pattern selects, or kNoColumn.
  //
  // The pattern is one bit per GPIO from strobeBasePin upwards. Exactly one
  // bit is set while a strobe is driven; the bit that falls on a GPIO which is
  // not a strobe - the on-board LED sits between two of them - selects nothing
  // and its sample is discarded.
  uint8_t columnOf(uint32_t pattern) const {
    if (pattern == 0 || (pattern & (pattern - 1)) != 0) {
      return kNoColumn;
    }
    uint8_t bit = 0;
    while (((pattern >> bit) & 1u) == 0) {
      bit++;
    }
    const uint8_t gpio = static_cast<uint8_t>(profile.strobeBasePin + bit);
    for (uint8_t column = 0; column < profile.strobes; column++) {
      if (profile.strobePins[column] == gpio) {
        return column;
      }
    }
    return kNoColumn;
  }

  // Takes one sample as the scan program pushes it: the strobe pattern in the
  // low strobeSpan bits, the returns above it.
  //
  // A change is reported once two consecutive scans of its column agree on it
  // and the lockout since the last edge in that direction has passed. Writes
  // up to maxEdges edges and returns how many.
  uint8_t decode(uint32_t word, uint64_t nowUs, Edge* edges, uint8_t maxEdges) {
    const uint32_t patternMask = (1u << profile.strobeSpan) - 1u;
    const uint8_t column = columnOf(word & patternMask);
    if (column == kNoColumn) {
      return 0;
    }

    const uint16_t rowMask =
        numRows >= 16 ? 0xFFFFu : static_cast<uint16_t>((1u << numRows) - 1u);
    const uint16_t raw = static_cast<uint16_t>(word >> profile.strobeSpan);
    const uint16_t closed =
        static_cast<uint16_t>((activeLow ? ~raw : raw) & rowMask);

    if (!haveCandidate[column]) {
      haveCandidate[column] = true;
      candidate[column] = closed;
      return 0;
    }

    const uint16_t agreed =
        static_cast<uint16_t>(~(closed ^ candidate[column]));
    uint16_t changed =
        static_cast<uint16_t>((closed ^ stable[column]) & agreed & rowMask);
    candidate[column] = closed;

    uint8_t count = 0;
    for (uint8_t row = 0; changed != 0 && row < numRows; row++) {
      const uint16_t bit = static_cast<uint16_t>(1u << row);
      if ((changed & bit) == 0) {
        continue;
      }
      changed = static_cast<uint16_t>(changed & ~bit);

      const uint8_t position = static_cast<uint8_t>(column * numRows + row);
      const uint8_t state = (closed & bit) ? 1 : 0;
      const uint8_t number = mapping[position];
      if (number == 0) {
        // Nobody asked about this position. Track it so it stops showing up
        // as a change, but there is nothing to report.
        stable[column] = static_cast<uint16_t>(stable[column] ^ bit);
        continue;
      }

      const uint64_t last = lastEdgeUs[position][state];
      if (last != 0 && (nowUs - last) < STROBED_MATRIX_DEBOUNCE_US) {
        continue;
      }
      if (count >= maxEdges) {
        // Not accepted, so it is seen again on the next scan.
        continue;
      }
      lastEdgeUs[position][state] = nowUs;
      stable[column] = static_cast<uint16_t>(stable[column] ^ bit);
      edges[count++] = {number, state};
    }
    return count;
  }

  // The stable state of every registered switch, for answering a full read.
  // Before the first scan that is "open" throughout.
  uint8_t snapshot(Edge* edges, uint8_t maxEdges) const {
    uint8_t count = 0;
    for (uint8_t column = 0; column < profile.strobes; column++) {
      for (uint8_t row = 0; row < numRows; row++) {
        const uint8_t position = static_cast<uint8_t>(column * numRows + row);
        if (mapping[position] == 0 || count >= maxEdges) {
          continue;
        }
        edges[count++] = {mapping[position],
                          static_cast<uint8_t>((stable[column] >> row) & 1u)};
      }
    }
    return count;
  }

 private:
  ppuc::board::StrobedMatrixProfile profile = {0, 0, 0, {0}, 0, 0};
  bool activeLow = false;
  bool registered = false;
  uint8_t numRows = 0;
  uint8_t mapping[kMaxColumns * kMaxRows] = {0};
  // Per column, one bit per row, 1 = closed.
  uint16_t candidate[kMaxColumns] = {0};
  bool haveCandidate[kMaxColumns] = {false};
  uint16_t stable[kMaxColumns] = {0};
  uint64_t lastEdgeUs[kMaxColumns * kMaxRows][2] = {{0}};
};

#endif
