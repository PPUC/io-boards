/*
  SwitchNumberStates.h

  The last state this board saw for each switch, kept by logical switch number.

  Play more pinball!
*/
#ifndef EVENTDISPATCHER_SwitchNumberStates_h
#define EVENTDISPATCHER_SwitchNumberStates_h

#include <stdint.h>
#include <string.h>

// What the outputs ask when they want to know whether a switch is closed: the
// fast-flip button that holds a flipper, the end-of-stroke contact that cuts
// its power winding.
//
// The dense switch bitmap could not answer that reliably. Its positions only
// mean something once the session's mapping frames are in, and every session
// resync starts again from the identity map: a Setup frame, 100 ms, then a
// mapping frame per coil, lamp and switch. In that window a switch numbered
// above the number of switch positions - Dirty Harry has 55, its flipper
// buttons are 112, 114 and 116 - resolved to no position and read open, and a
// lower number read whichever switch sat at the position equal to its number.
// PwmDevices asks on every update, so a held flipper dropped at every resync,
// a press during one was cut off again, and the flipper only came up once the
// mapping was complete and the board had re-read its inputs.
//
// A switch's number does not depend on the mapping, so neither does this. It
// is fed from every switch event the board processes and from the raw re-read
// of the dedicated inputs, and only a restart or a reset clears it.
class SwitchNumberStates {
 public:
  // Switch numbers the outputs can be configured with are bytes, so this
  // covers every one of them. A higher number falls back to the bitmap.
  static constexpr uint16_t kNumbers = 256;

  static bool covers(uint16_t number) { return number < kNumbers; }

  bool get(uint16_t number) const {
    if (!covers(number)) return false;
    return (m_closed[number >> 3] >> (number & 7)) & 1u;
  }

  void record(uint16_t number, bool closed) {
    if (!covers(number)) return;
    const uint8_t mask = static_cast<uint8_t>(1u << (number & 7));
    if (closed) {
      m_closed[number >> 3] |= mask;
    } else {
      m_closed[number >> 3] &= static_cast<uint8_t>(~mask);
    }
  }

  void clear() { memset(m_closed, 0, sizeof(m_closed)); }

 private:
  uint8_t m_closed[kNumbers / 8] = {0};
};

#endif
