/*
  MappingProgress.h

  Which of a session's mapping positions have arrived.

  Play more pinball!
*/
#ifndef EVENTDISPATCHER_MappingProgress_h
#define EVENTDISPATCHER_MappingProgress_h

#include <stdint.h>
#include <string.h>

#include "../PPUCProtocolV2.h"

// A session's mapping is complete once every coil, lamp and switch position
// has been named, not once as many mapping frames as there are positions have
// arrived.
//
// Counting frames was wrong both ways. A frame the host repeated, or one for a
// position outside the session, counted as well, so a lost frame could be
// made up for by another one and the board called a mapping complete that was
// not. The position whose frame was lost then kept the identity map, and a bit
// meant for one coil drove another: on Dirty Harry, the game-on solenoid's bit
// could land on coil 26 and hold it on.
class MappingProgress {
 public:
  void reset(uint16_t coilBits, uint16_t lampBits, uint16_t switchBits) {
    memset(m_coils, 0, sizeof(m_coils));
    memset(m_lamps, 0, sizeof(m_lamps));
    memset(m_switches, 0, sizeof(m_switches));
    m_coilBits = coilBits;
    m_lampBits = lampBits;
    m_switchBits = switchBits;
    m_expected = static_cast<uint16_t>(coilBits + lampBits + switchBits);
    m_received = 0;
  }

  // Notes one mapping frame. Returns whether it named a position of this
  // session, which is when its number may be used.
  bool note(uint8_t domain, uint16_t index) {
    uint8_t* seen = nullptr;
    if (domain == ppuc::v2::kDomainCoil && index < m_coilBits) {
      seen = m_coils;
    } else if (domain == ppuc::v2::kDomainLamp && index < m_lampBits) {
      seen = m_lamps;
    } else if (domain == ppuc::v2::kDomainSwitch && index < m_switchBits) {
      seen = m_switches;
    } else {
      return false;
    }
    if (!ppuc::v2::GetBitmapBit(seen, index)) {
      ppuc::v2::SetBitmapBit(seen, index, true);
      ++m_received;
    }
    return true;
  }

  bool complete() const { return m_received >= m_expected; }
  uint16_t expected() const { return m_expected; }
  uint16_t received() const { return m_received; }

 private:
  uint8_t m_coils[ppuc::v2::kMaxCoilBytes] = {0};
  uint8_t m_lamps[ppuc::v2::kMaxLampBytes] = {0};
  uint8_t m_switches[ppuc::v2::kMaxSwitchBytes] = {0};
  uint16_t m_coilBits = 0;
  uint16_t m_lampBits = 0;
  uint16_t m_switchBits = 0;
  uint16_t m_expected = 0;
  uint16_t m_received = 0;
};

#endif
