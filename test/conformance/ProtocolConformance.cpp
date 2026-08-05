#include "ProtocolConformance.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "PPUCProtocolV2.h"

namespace ppuc_conformance {
namespace {

using namespace ppuc::v2;

Result Pass() { return Result{true, {}}; }

Result Fail(const std::string& detail) { return Result{false, detail}; }

std::string Num(long long v) { return std::to_string(v); }

// Packs a type/flag pair the way the wire format does.
//
// Deliberately not ComposeTypeAndFlags(): this suite exists to pin the wire
// format, so it must not derive its expectations from the helper it is
// checking. The explicit casts also keep GCC quiet - a bare
// `kFrameMapping | kFlagKeyframe` is a bitwise op between two distinct
// enumeration types, which C++20 deprecates
// (-Wdeprecated-enum-enum-conversion).
uint8_t Packed(FrameType type, FrameFlag flag) {
  return static_cast<uint8_t>(static_cast<uint8_t>(type) |
                              static_cast<uint8_t>(flag));
}

// --- constants and sizing ---------------------------------------------------

Result CheckWireConstants() {
  if (kSyncByte != 0xA5) return Fail("sync byte changed: " + Num(kSyncByte));
  if (kHeaderBytes != 5) return Fail("header size changed: " + Num(kHeaderBytes));
  if (kCrcBytes != 2) return Fail("CRC size changed: " + Num(kCrcBytes));
  if (kNoBoard != 0xFF) return Fail("kNoBoard changed: " + Num(kNoBoard));
  if (kMaxBoards != 8) return Fail("kMaxBoards changed: " + Num(kMaxBoards));
  if (kGiStrings != 5) return Fail("GI string count changed: " + Num(kGiStrings));
  if (kGiLevelBits != 4) return Fail("GI level bits changed: " + Num(kGiLevelBits));
  if (kMaxGiLevel != 8) return Fail("max GI level changed: " + Num(kMaxGiLevel));
  return Pass();
}

Result CheckFrameTypeValues() {
  // Frame type values are on the wire. Renumbering breaks every deployed board.
  const struct {
    FrameType type;
    uint8_t expected;
    const char* name;
  } kExpected[] = {
      {kFrameOutputState, 0x01, "OutputState"},
      {kFrameSwitchState, 0x02, "SwitchState"},
      {kFrameHeartbeat, 0x03, "Heartbeat"},
      {kFrameError, 0x04, "Error"},
      {kFrameSetup, 0x05, "Setup"},
      {kFrameMapping, 0x06, "Mapping"},
      {kFrameReset, 0x07, "Reset"},
      {kFrameConfig, 0x08, "Config"},
      {kFrameSwitchNoChange, 0x09, "SwitchNoChange"},
      {kFrameConfigAck, 0x0A, "ConfigAck"},
      {kFrameRestart, 0x0B, "Restart"},
      {kFrameTrigger, 0x0C, "Trigger"},
      {kFrameSwitchRefresh, 0x0D, "SwitchRefresh"},
  };

  for (const auto& e : kExpected) {
    if (static_cast<uint8_t>(e.type) != e.expected) {
      return Fail(std::string(e.name) + " changed value: expected " +
                  Num(e.expected) + ", got " + Num(e.type));
    }
  }
  return Pass();
}

Result CheckBitsToBytes() {
  // Boundaries: a bit count that exactly fills a byte must not allocate one
  // more, and one bit over must.
  const struct {
    uint16_t bits;
    size_t expected;
  } kExpected[] = {{0, 0},   {1, 1},   {7, 1},   {8, 1},   {9, 2},
                   {15, 2},  {16, 2},  {17, 3},  {64, 8},  {65, 9},
                   {255, 32}, {256, 32}};

  for (const auto& e : kExpected) {
    if (BitsToBytes(e.bits) != e.expected) {
      return Fail("BitsToBytes(" + Num(e.bits) + ") = " +
                  Num(BitsToBytes(e.bits)) + ", expected " + Num(e.expected));
    }
  }
  return Pass();
}

Result CheckGiPayloadSizing() {
  // 5 strings at 4 bits each is 20 bits, which must round up to 3 bytes.
  if (kGiBytes != 3) return Fail("kGiBytes = " + Num(kGiBytes) + ", expected 3");
  return Pass();
}

Result CheckDefaultsWithinMaxima() {
  if (kDefaultCoilBits > kMaxCoilBits) return Fail("default coil bits exceed max");
  if (kDefaultLampBits > kMaxLampBits) return Fail("default lamp bits exceed max");
  if (kDefaultSwitchBits > kMaxSwitchBits) return Fail("default switch bits exceed max");
  return Pass();
}

Result CheckFrameSizesAreConsistent() {
  // Every fixed frame size must equal header + payload + CRC. A hand-edited
  // constant that drifts from its payload is a silent framing bug.
  const struct {
    size_t frame;
    size_t payload;
    const char* name;
  } kExpected[] = {
      {kSetupFrameBytes, kSetupPayloadBytes, "Setup"},
      {kMappingFrameBytes, kMappingPayloadBytes, "Mapping"},
      {kConfigFrameBytes, kConfigPayloadBytes, "Config"},
      {kOutputFrameBytes, kOutputPayloadBytes, "Output"},
      {kConfigAckFrameBytes, kConfigAckPayloadBytes, "ConfigAck"},
      {kSwitchFrameBytes, kSwitchPayloadBytes, "Switch"},
      {kTriggerFrameBytes, kTriggerPayloadBytes, "Trigger"},
  };

  for (const auto& e : kExpected) {
    const size_t want = kHeaderBytes + e.payload + kCrcBytes;
    if (e.frame != want) {
      return Fail(std::string(e.name) + "FrameBytes = " + Num(e.frame) +
                  ", expected " + Num(want));
    }
  }

  // Payload-free frames are header + CRC only.
  const size_t bare = kHeaderBytes + kCrcBytes;
  if (kResetFrameBytes != bare) return Fail("Reset frame size wrong");
  if (kRestartFrameBytes != bare) return Fail("Restart frame size wrong");
  if (kSwitchRefreshFrameBytes != bare) return Fail("SwitchRefresh frame size wrong");
  return Pass();
}

Result CheckRuntimeSizing() {
  RuntimeConfig cfg;
  cfg.coilBits = 24;
  cfg.lampBits = 64;
  cfg.switchBits = 56;

  const size_t wantOutput = 3 + 8 + kGiBytes;  // 24 bits, 64 bits, GI
  if (OutputPayloadBytes(cfg) != wantOutput) {
    return Fail("OutputPayloadBytes = " + Num(OutputPayloadBytes(cfg)) +
                ", expected " + Num(wantOutput));
  }

  const size_t wantSwitch = kSwitchStatusBytes + 7;  // 56 bits
  if (SwitchPayloadBytes(cfg) != wantSwitch) {
    return Fail("SwitchPayloadBytes = " + Num(SwitchPayloadBytes(cfg)) +
                ", expected " + Num(wantSwitch));
  }

  if (OutputFrameBytes(cfg) != kHeaderBytes + wantOutput + kCrcBytes) {
    return Fail("OutputFrameBytes inconsistent with its payload");
  }
  if (SwitchFrameBytes(cfg) != kHeaderBytes + wantSwitch + kCrcBytes) {
    return Fail("SwitchFrameBytes inconsistent with its payload");
  }
  if (SwitchNoChangeFrameBytes() != kHeaderBytes + kSwitchStatusBytes + kCrcBytes) {
    return Fail("SwitchNoChangeFrameBytes inconsistent");
  }
  return Pass();
}

Result CheckRuntimeConfigValidation() {
  RuntimeConfig ok;
  if (!IsValidRuntimeConfig(ok)) return Fail("the default RuntimeConfig is rejected");

  RuntimeConfig zeroCoils = ok;
  zeroCoils.coilBits = 0;
  if (IsValidRuntimeConfig(zeroCoils)) return Fail("zero coil bits accepted");

  RuntimeConfig tooManyCoils = ok;
  tooManyCoils.coilBits = kMaxCoilBits + 1;
  if (IsValidRuntimeConfig(tooManyCoils)) return Fail("coil bits above max accepted");

  RuntimeConfig tooManyLamps = ok;
  tooManyLamps.lampBits = kMaxLampBits + 1;
  if (IsValidRuntimeConfig(tooManyLamps)) return Fail("lamp bits above max accepted");

  RuntimeConfig tooManySwitches = ok;
  tooManySwitches.switchBits = kMaxSwitchBits + 1;
  if (IsValidRuntimeConfig(tooManySwitches)) return Fail("switch bits above max accepted");

  return Pass();
}

// --- CRC --------------------------------------------------------------------

Result CheckCrcKnownAnswers() {
  // CRC-16/CCITT-FALSE: init 0xFFFF, poly 0x1021, no reflection, no final xor.
  // "123456789" is the standard check vector for this parameterisation.
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  const uint16_t crc = Crc16Ccitt(check, sizeof(check));
  if (crc != 0x29B1) {
    return Fail("CRC-16/CCITT-FALSE check vector failed: got 0x" +
                Num(crc) + ", expected 0x29B1 (10673). The CRC "
                "parameterisation changed, which breaks every deployed board.");
  }

  const uint8_t empty[] = {0};
  if (Crc16Ccitt(empty, 0) != 0xFFFF) {
    return Fail("CRC of an empty buffer must be the 0xFFFF init value");
  }
  return Pass();
}

Result CheckCrcDetectsCorruption() {
  uint8_t frame[16];
  for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = static_cast<uint8_t>(i);
  const uint16_t good = Crc16Ccitt(frame, sizeof(frame));

  // Every single-bit flip must change the CRC. This is the property the bus
  // relies on to reject a frame corrupted during turnaround.
  for (size_t byte = 0; byte < sizeof(frame); ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      frame[byte] ^= static_cast<uint8_t>(1u << bit);
      const uint16_t bad = Crc16Ccitt(frame, sizeof(frame));
      frame[byte] ^= static_cast<uint8_t>(1u << bit);
      if (bad == good) {
        return Fail("a single-bit flip at byte " + Num(byte) + " bit " +
                    Num(bit) + " did not change the CRC");
      }
    }
  }
  return Pass();
}

// --- type and flag packing --------------------------------------------------

Result CheckTypeAndFlagRoundTrip() {
  const FrameType kTypes[] = {
      kFrameOutputState, kFrameSwitchState, kFrameHeartbeat, kFrameError,
      kFrameSetup,       kFrameMapping,     kFrameReset,     kFrameConfig,
      kFrameSwitchNoChange, kFrameConfigAck, kFrameRestart,  kFrameTrigger,
      kFrameSwitchRefresh};
  const uint8_t kFlags[] = {kFlagNone, kFlagKeyframe, kFlagDelta, kFlagError,
                            static_cast<uint8_t>(kFlagKeyframe | kFlagError)};

  for (FrameType type : kTypes) {
    for (uint8_t flags : kFlags) {
      const uint8_t packed = ComposeTypeAndFlags(type, flags);
      if (ExtractType(packed) != type) {
        return Fail("type " + Num(type) + " with flags " + Num(flags) +
                    " did not survive the round trip");
      }
      if (ExtractFlags(packed) != flags) {
        return Fail("flags " + Num(flags) + " with type " + Num(type) +
                    " did not survive the round trip");
      }
    }
  }
  return Pass();
}

Result CheckBoardValidation() {
  for (uint8_t board = 0; board < kMaxBoards; ++board) {
    if (!IsValidBoard(board)) return Fail("board " + Num(board) + " rejected");
  }
  if (!IsValidBoard(kNoBoard)) return Fail("kNoBoard must be a valid token");
  if (IsValidBoard(kMaxBoards)) return Fail("board == kMaxBoards accepted");
  if (IsValidBoard(kMaxBoards + 1)) return Fail("board above kMaxBoards accepted");
  return Pass();
}

// --- bitmaps ----------------------------------------------------------------

Result CheckBitmapRoundTrip() {
  uint8_t bitmap[kMaxLampBytes];
  std::memset(bitmap, 0, sizeof(bitmap));

  // Set every bit in turn, confirm it reads back and that no neighbour moved.
  for (uint16_t n = 0; n < kMaxLampBits; ++n) {
    SetBitmapBit(bitmap, n, true);
    if (!GetBitmapBit(bitmap, n)) {
      return Fail("bit " + Num(n) + " did not read back as set");
    }
    for (uint16_t other = 0; other < kMaxLampBits; ++other) {
      if (other == n) continue;
      if (GetBitmapBit(bitmap, other)) {
        return Fail("setting bit " + Num(n) + " also set bit " + Num(other));
      }
    }
    SetBitmapBit(bitmap, n, false);
    if (GetBitmapBit(bitmap, n)) {
      return Fail("bit " + Num(n) + " did not clear");
    }
  }
  return Pass();
}

Result CheckBitmapByteOrder() {
  // Bit N lives in byte N/8 at position N%8. Both sides index bitmaps this way,
  // so the layout itself is part of the wire contract.
  uint8_t bitmap[4] = {0, 0, 0, 0};
  SetBitmapBit(bitmap, 0, true);
  if (bitmap[0] != 0x01) return Fail("bit 0 must be byte 0 bit 0");
  SetBitmapBit(bitmap, 0, false);

  SetBitmapBit(bitmap, 7, true);
  if (bitmap[0] != 0x80) return Fail("bit 7 must be byte 0 bit 7");
  SetBitmapBit(bitmap, 7, false);

  SetBitmapBit(bitmap, 8, true);
  if (bitmap[1] != 0x01) return Fail("bit 8 must be byte 1 bit 0");
  SetBitmapBit(bitmap, 8, false);

  SetBitmapBit(bitmap, 17, true);
  if (bitmap[2] != 0x02) return Fail("bit 17 must be byte 2 bit 1");
  return Pass();
}

// --- GI nibble packing ------------------------------------------------------

Result CheckGiNibbleRoundTrip() {
  uint8_t gi[kGiBytes];
  std::memset(gi, 0, sizeof(gi));

  // Every string at every valid level, with all strings written at once so the
  // shared byte between odd and even indices is exercised.
  for (uint8_t level = 0; level <= kMaxGiLevel; ++level) {
    for (uint8_t s = 0; s < kGiStrings; ++s) {
      SetPackedNibble(gi, s, level);
    }
    for (uint8_t s = 0; s < kGiStrings; ++s) {
      if (GetPackedNibble(gi, s) != level) {
        return Fail("GI string " + Num(s) + " at level " + Num(level) +
                    " read back as " + Num(GetPackedNibble(gi, s)));
      }
    }
  }
  return Pass();
}

Result CheckGiNibblesAreIndependent() {
  // Two GI strings share one byte. Writing one must not disturb the other -
  // this is exactly the kind of masking bug that shows up as a flickering GI
  // string on a real machine.
  uint8_t gi[kGiBytes];
  std::memset(gi, 0, sizeof(gi));

  for (uint8_t s = 0; s < kGiStrings; ++s) {
    SetPackedNibble(gi, s, static_cast<uint8_t>(s + 1));
  }
  for (uint8_t s = 0; s < kGiStrings; ++s) {
    if (GetPackedNibble(gi, s) != static_cast<uint8_t>(s + 1)) {
      return Fail("GI string " + Num(s) + " was disturbed by a neighbour: got " +
                  Num(GetPackedNibble(gi, s)) + ", expected " + Num(s + 1));
    }
  }

  // Overwrite one and confirm the rest are untouched.
  SetPackedNibble(gi, 2, 0);
  for (uint8_t s = 0; s < kGiStrings; ++s) {
    const uint8_t want = (s == 2) ? 0 : static_cast<uint8_t>(s + 1);
    if (GetPackedNibble(gi, s) != want) {
      return Fail("overwriting GI string 2 disturbed string " + Num(s));
    }
  }
  return Pass();
}

Result CheckGiClamping() {
  for (uint8_t level = 0; level <= kMaxGiLevel; ++level) {
    if (ClampGiLevel(level) != level) {
      return Fail("level " + Num(level) + " must pass through unchanged");
    }
  }
  for (int level = kMaxGiLevel + 1; level <= 255; ++level) {
    if (ClampGiLevel(static_cast<uint8_t>(level)) != kMaxGiLevel) {
      return Fail("level " + Num(level) + " must clamp to " + Num(kMaxGiLevel));
    }
  }

  // A clamped level must always fit the 4-bit wire field.
  uint8_t gi[kGiBytes] = {0, 0, 0};
  SetPackedNibble(gi, 0, ClampGiLevel(255));
  if (GetPackedNibble(gi, 0) != kMaxGiLevel) {
    return Fail("a clamped level did not survive nibble packing");
  }
  return Pass();
}


// --- wire codec -------------------------------------------------------------

Result CheckBigEndianPrimitives() {
  uint8_t buf[4] = {0, 0, 0, 0};

  WriteU16(buf, 0x1234);
  if (buf[0] != 0x12 || buf[1] != 0x34) {
    return Fail("WriteU16 must be big-endian: got " + Num(buf[0]) + "," +
                Num(buf[1]));
  }
  if (ReadU16(buf) != 0x1234) return Fail("ReadU16 did not round-trip");

  WriteU32(buf, 0x12345678u);
  if (buf[0] != 0x12 || buf[1] != 0x34 || buf[2] != 0x56 || buf[3] != 0x78) {
    return Fail("WriteU32 must be big-endian");
  }
  if (ReadU32(buf) != 0x12345678u) return Fail("ReadU32 did not round-trip");

  // Edges: a value with the high bit set must not sign-extend.
  WriteU16(buf, 0xFFFF);
  if (ReadU16(buf) != 0xFFFF) return Fail("ReadU16 mishandles 0xFFFF");
  WriteU32(buf, 0xFFFFFFFFu);
  if (ReadU32(buf) != 0xFFFFFFFFu) return Fail("ReadU32 mishandles 0xFFFFFFFF");
  return Pass();
}

Result CheckMappingFrameByteLayout() {
  // Pins the exact bytes on the wire. libppuc built this frame by hand before
  // the codec existed; these positions are what deployed boards already parse,
  // so a change here is a breaking protocol change, not a refactor.
  uint8_t frame[kMappingFrameBytes];
  const size_t len = BuildMappingFrame(frame, kNoBoard, /*seq*/ 0x2A,
                                       /*epoch*/ 0x07, kDomainLamp,
                                       /*index*/ 0x0102, /*number*/ 0x0304);

  if (len != kMappingFrameBytes) {
    return Fail("BuildMappingFrame returned " + Num(len) + ", expected " +
                Num(kMappingFrameBytes));
  }
  if (frame[0] != kSyncByte) return Fail("byte 0 must be the sync byte");
  if (frame[1] != Packed(kFrameMapping, kFlagKeyframe)) {
    return Fail("byte 1 must be type|flags, got " + Num(frame[1]));
  }
  if (frame[2] != kNoBoard) return Fail("byte 2 must be nextBoard");
  if (frame[3] != 0x2A) return Fail("byte 3 must be the sequence");
  if (frame[4] != 0x07) return Fail("byte 4 must be the epoch");
  if (frame[5] != kDomainLamp) return Fail("byte 5 must be the mapping domain");
  if (frame[6] != 0) return Fail("byte 6 is reserved and must be zero");
  if (frame[7] != 0x01 || frame[8] != 0x02) {
    return Fail("bytes 7-8 must be the index, big-endian");
  }
  if (frame[9] != 0x03 || frame[10] != 0x04) {
    return Fail("bytes 9-10 must be the number, big-endian");
  }
  if (!VerifyCrc(frame, len)) return Fail("the frame failed its own CRC check");
  return Pass();
}

Result CheckSetupFrameByteLayout() {
  uint8_t frame[kSetupFrameBytes];
  RuntimeConfig cfg;
  cfg.coilBits = 0x0018;   // 24
  cfg.lampBits = 0x0040;   // 64
  cfg.switchBits = 0x0038; // 56

  const size_t len = BuildSetupFrame(frame, kNoBoard, 1, 1, cfg);
  if (len != kSetupFrameBytes) return Fail("BuildSetupFrame length wrong");
  if (frame[1] != Packed(kFrameSetup, kFlagKeyframe)) return Fail("setup type byte wrong");
  if (frame[5] != 0x00 || frame[6] != 0x18) return Fail("coilBits must be big-endian");
  if (frame[7] != 0x00 || frame[8] != 0x40) return Fail("lampBits must be big-endian");
  if (frame[9] != 0x00 || frame[10] != 0x38) return Fail("switchBits must be big-endian");
  if (!VerifyCrc(frame, len)) return Fail("setup frame failed its own CRC check");

  RuntimeConfig decoded;
  ReadSetupPayload(frame + kHeaderBytes, decoded);
  if (decoded.coilBits != cfg.coilBits || decoded.lampBits != cfg.lampBits ||
      decoded.switchBits != cfg.switchBits) {
    return Fail("setup payload did not round-trip");
  }
  return Pass();
}

Result CheckConfigFrameByteLayout() {
  uint8_t frame[kConfigFrameBytes];
  const size_t len = BuildConfigFrame(frame, kNoBoard, 5, 2, /*board*/ 3,
                                      /*topic*/ 115, /*index*/ 9, /*key*/ 77,
                                      /*value*/ 0xDEADBEEFu);
  if (len != kConfigFrameBytes) return Fail("BuildConfigFrame length wrong");
  if (frame[5] != 3) return Fail("byte 5 must be the board id");
  if (frame[6] != 115) return Fail("byte 6 must be the topic");
  if (frame[7] != 9) return Fail("byte 7 must be the index");
  if (frame[8] != 77) return Fail("byte 8 must be the key");
  if (frame[9] != 0xDE || frame[10] != 0xAD || frame[11] != 0xBE ||
      frame[12] != 0xEF) {
    return Fail("bytes 9-12 must be the 32-bit value, big-endian");
  }
  if (!VerifyCrc(frame, len)) return Fail("config frame failed its own CRC check");

  uint8_t board, topic, index, key;
  uint32_t value;
  ReadConfigPayload(frame + kHeaderBytes, board, topic, index, key, value);
  if (board != 3 || topic != 115 || index != 9 || key != 77 ||
      value != 0xDEADBEEFu) {
    return Fail("config payload did not round-trip");
  }
  return Pass();
}

Result CheckHeaderRoundTrip() {
  uint8_t frame[kMappingFrameBytes];
  BuildMappingFrame(frame, 3, 99, 4, kDomainCoil, 7, 42);

  FrameHeader header;
  if (!ReadHeader(frame, header)) return Fail("ReadHeader rejected a valid frame");
  if (header.nextBoard != 3) return Fail("nextBoard did not round-trip");
  if (header.sequence != 99) return Fail("sequence did not round-trip");
  if (header.epoch != 4) return Fail("epoch did not round-trip");
  if (ExtractType(header.typeAndFlags) != kFrameMapping) {
    return Fail("frame type did not round-trip");
  }

  // A bad sync byte must be refused so the caller resynchronises.
  frame[0] = 0x00;
  if (ReadHeader(frame, header)) return Fail("ReadHeader accepted a bad sync byte");
  return Pass();
}

Result CheckCrcRejectsTamperedFrames() {
  uint8_t frame[kConfigFrameBytes];
  const size_t len = BuildConfigFrame(frame, kNoBoard, 1, 1, 2, 3, 4, 5, 6);
  if (!VerifyCrc(frame, len)) return Fail("a freshly built frame failed its CRC");

  // Every byte of header and payload must be covered by the CRC.
  for (size_t i = 0; i < len - kCrcBytes; ++i) {
    frame[i] ^= 0x01;
    const bool stillValid = VerifyCrc(frame, len);
    frame[i] ^= 0x01;
    if (stillValid) {
      return Fail("tampering with byte " + Num(i) + " was not detected");
    }
  }

  // A truncated frame must not be accepted.
  if (VerifyCrc(frame, kHeaderBytes + kCrcBytes - 1)) {
    return Fail("a frame shorter than header+CRC was accepted");
  }
  return Pass();
}

Result CheckBareFrameLayout() {
  uint8_t frame[kResetFrameBytes];
  const size_t len = BuildBareFrame(frame, kFrameReset, kFlagNone, kNoBoard,
                                    /*seq*/ 11, /*epoch*/ 3);
  if (len != kResetFrameBytes) return Fail("bare frame length wrong");
  if (frame[1] != kFrameReset) return Fail("Reset must carry no flags");
  if (frame[3] != 11 || frame[4] != 3) return Fail("sequence/epoch wrong");
  if (!VerifyCrc(frame, len)) return Fail("bare frame failed its own CRC");
  return Pass();
}

Result CheckTriggerFrameLayout() {
  uint8_t frame[kTriggerFrameBytes];
  const size_t len = BuildTriggerFrame(frame, kNoBoard, 4, 1, /*source*/ 'F',
                                       /*number*/ 0x0142, /*value*/ 9);
  if (len != kTriggerFrameBytes) return Fail("trigger frame length wrong");
  if (frame[1] != kFrameTrigger) return Fail("Trigger must carry no flags");
  if (frame[5] != 'F') return Fail("byte 5 must be the event source");
  if (frame[6] != 0x01 || frame[7] != 0x42) {
    return Fail("bytes 6-7 must be the number, big-endian");
  }
  if (frame[8] != 9) return Fail("byte 8 must be the value");
  if (!VerifyCrc(frame, len)) return Fail("trigger frame failed its own CRC");

  uint8_t source, value;
  uint16_t number;
  ReadTriggerPayload(frame + kHeaderBytes, source, number, value);
  if (source != 'F' || number != 0x0142 || value != 9) {
    return Fail("trigger payload did not round-trip");
  }
  return Pass();
}

Result CheckSwitchReplyLayout() {
  uint8_t bitmap[8] = {0xAA, 0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  uint8_t frame[kHeaderBytes + kSwitchStatusBytes + kMaxSwitchBytes + kCrcBytes];

  // No-change: status prefix only.
  size_t len = BuildSwitchReplyFrame(frame, /*sendState*/ false, 2, 7, 1, 1, 7,
                                     kStatusInSync, nullptr, 0);
  if (len != kHeaderBytes + kSwitchStatusBytes + kCrcBytes) {
    return Fail("SwitchNoChange length wrong: " + Num(len));
  }
  if (frame[1] != kFrameSwitchNoChange) return Fail("no-change must carry no flags");
  if (frame[5] != 1) return Fail("byte 5 must be epochSeen");
  if (frame[6] != 7) return Fail("byte 6 must be lastHostSequenceSeen");
  if (frame[7] != kStatusInSync) return Fail("byte 7 must be the status flags");
  if (frame[8] != 0) return Fail("byte 8 is reserved and must be zero");
  if (!VerifyCrc(frame, len)) return Fail("no-change frame failed its own CRC");

  // State: status prefix plus the bitmap.
  len = BuildSwitchReplyFrame(frame, /*sendState*/ true, 2, 7, 1, 1, 7,
                              kStatusInSync, bitmap, sizeof(bitmap));
  if (len != kHeaderBytes + kSwitchStatusBytes + sizeof(bitmap) + kCrcBytes) {
    return Fail("SwitchState length wrong: " + Num(len));
  }
  if (frame[1] != Packed(kFrameSwitchState, kFlagKeyframe)) {
    return Fail("SwitchState must be a keyframe");
  }
  for (size_t i = 0; i < sizeof(bitmap); ++i) {
    if (frame[kHeaderBytes + kSwitchStatusBytes + i] != bitmap[i]) {
      return Fail("switch bitmap byte " + Num(i) + " was not copied verbatim");
    }
  }
  if (!VerifyCrc(frame, len)) return Fail("switch state frame failed its own CRC");
  return Pass();
}

Result CheckOutputStateLayout() {
  RuntimeConfig cfg;
  cfg.coilBits = 24;   // 3 bytes
  cfg.lampBits = 64;   // 8 bytes
  cfg.switchBits = 64;

  uint8_t coils[3] = {0x01, 0x02, 0x03};
  uint8_t lamps[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
  uint8_t gi[kGiStrings] = {0, 1, 8, 15, 4};  // 15 must clamp to 8

  uint8_t frame[kHeaderBytes + kMaxCoilBytes + kMaxLampBytes + kGiBytes + kCrcBytes];
  const size_t len = BuildOutputStateFrame(frame, /*nextBoard*/ 1, 33, 2, cfg,
                                           coils, lamps, gi);

  if (len != OutputFrameBytes(cfg)) {
    return Fail("output frame length " + Num(len) + " != OutputFrameBytes " +
                Num(OutputFrameBytes(cfg)));
  }
  if (frame[1] != Packed(kFrameOutputState, kFlagKeyframe)) {
    return Fail("OutputState must be a keyframe");
  }
  for (size_t i = 0; i < sizeof(coils); ++i) {
    if (frame[kHeaderBytes + i] != coils[i]) {
      return Fail("coil byte " + Num(i) + " not copied verbatim");
    }
  }
  for (size_t i = 0; i < sizeof(lamps); ++i) {
    if (frame[kHeaderBytes + sizeof(coils) + i] != lamps[i]) {
      return Fail("lamp byte " + Num(i) + " not copied verbatim");
    }
  }

  const uint8_t* giOut = frame + kHeaderBytes + sizeof(coils) + sizeof(lamps);
  const uint8_t wantGi[kGiStrings] = {0, 1, 8, kMaxGiLevel, 4};
  for (uint8_t s = 0; s < kGiStrings; ++s) {
    if (GetPackedNibble(giOut, s) != wantGi[s]) {
      return Fail("GI string " + Num(s) + " encoded as " +
                  Num(GetPackedNibble(giOut, s)) + ", expected " +
                  Num(wantGi[s]) + " (out-of-range levels must clamp)");
    }
  }
  if (!VerifyCrc(frame, len)) return Fail("output frame failed its own CRC");
  return Pass();
}

Result CheckWireSizesAreNotStructPadding() {
  // Companion to the static_asserts in the header. The wire format is built
  // byte at a time so endianness and packing cannot affect the *layout*, but
  // the frame length constants come from sizeof() on structs that are never
  // serialized. Padding would change the length while the layout stayed right.
  const struct {
    size_t actual;
    size_t wire;
    const char* name;
  } kExpected[] = {
      {kSetupPayloadBytes, 6, "SetupPayload"},
      {kMappingPayloadBytes, 6, "MappingPayload"},
      {kConfigPayloadBytes, 8, "ConfigPayload"},
      {kConfigAckPayloadBytes, 8, "ConfigAckPayload"},
      {kTriggerPayloadBytes, 4, "TriggerPayload"},
      {kSwitchStatusBytes, 4, "switch status prefix"},
      {kGiBytes, 3, "GI payload"},
  };

  for (const auto& e : kExpected) {
    if (e.actual != e.wire) {
      return Fail(std::string(e.name) + " is " + Num(e.actual) +
                  " bytes but the wire format is " + Num(e.wire) +
                  ". This compiler is padding a payload struct, so frames "
                  "would be sent at the wrong length.");
    }
  }
  return Pass();
}

Result CheckCodecIsEndianIndependent() {
  // Writing a known value and reading back the individual bytes proves the
  // codec does not depend on host byte order: the assertions are on byte
  // positions, which are identical on a big- or little-endian host.
  uint8_t buf[8];
  WriteU32(buf, 0x01020304u);
  if (buf[0] != 0x01 || buf[1] != 0x02 || buf[2] != 0x03 || buf[3] != 0x04) {
    return Fail("WriteU32 produced host byte order rather than big-endian");
  }

  // And reading a hand-laid big-endian buffer must give the same value on any
  // host, which is what makes an x86 laptop and an RP2040 interoperate.
  const uint8_t wire[4] = {0x0A, 0x0B, 0x0C, 0x0D};
  if (ReadU32(wire) != 0x0A0B0C0Du) {
    return Fail("ReadU32 did not decode a big-endian buffer correctly");
  }
  const uint8_t wire16[2] = {0x0A, 0x0B};
  if (ReadU16(wire16) != 0x0A0Bu) {
    return Fail("ReadU16 did not decode a big-endian buffer correctly");
  }
  return Pass();
}
}  // namespace

Result CheckAdminFrameLayout() {
  uint8_t frame[kAdminFrameBytes];
  uint8_t data[kAdminDataBytes] = {0};
  data[0] = 0xAA;
  data[kAdminDataBytes - 1] = 0x55;

  const size_t len = BuildAdminFrame(frame, kAdminVersionQuery, /*boardId*/ 3,
                                     kNoBoard, /*sequence*/ 7, /*epoch*/ 2,
                                     data);
  if (len != kAdminFrameBytes) return Fail("admin frame length wrong");
  if (frame[1] != kFrameAdmin) return Fail("Admin must carry no flags");
  if (frame[5] != kAdminVersionQuery) {
    return Fail("byte 5 must be the admin command");
  }
  if (frame[6] != 3) return Fail("byte 6 must be the board id");
  // Derived from the constant rather than hardcoded: the data area has grown
  // once already, and a test that pins the old extent fails for the wrong
  // reason when it grows again.
  const size_t dataStart = kHeaderBytes + kAdminPrefixBytes;
  if (frame[dataStart] != 0xAA ||
      frame[dataStart + kAdminDataBytes - 1] != 0x55) {
    return Fail("the admin data area is not where the layout says it is");
  }
  if (!VerifyCrc(frame, len)) return Fail("admin frame failed its own CRC");

  uint8_t command = 0, boardId = 0;
  uint8_t out[kAdminDataBytes] = {0};
  ReadAdminPayload(frame + kHeaderBytes, command, boardId, out);
  if (command != kAdminVersionQuery || boardId != 3) {
    return Fail("admin header fields did not round-trip");
  }
  for (size_t i = 0; i < kAdminDataBytes; ++i) {
    if (out[i] != data[i]) return Fail("admin data did not round-trip");
  }
  return Pass();
}

Result CheckVersionReportLayout() {
  uint8_t frame[kAdminFrameBytes];
  const size_t len = BuildVersionReportFrame(frame, /*boardId*/ 5,
                                             /*sequence*/ 1, /*epoch*/ 1,
                                             /*fw*/ 2, 3, 4,
                                             kAdminCapabilityVersionReport,
                                             kBoardTypeIo16_8_1,
                                             /*buildId*/ 0xDEADBEEF);
  if (len != kAdminFrameBytes) return Fail("version report length wrong");
  if (ExtractType(frame[1]) != kFrameAdmin) {
    return Fail("version report must be an admin frame");
  }
  if (frame[5] != kAdminVersionReport) return Fail("wrong admin command");
  if (frame[6] != 5) return Fail("version report must name its board");

  const uint8_t* data = frame + kHeaderBytes + 2;
  if (data[kAdminVersionFirmwareMajor] != 2 ||
      data[kAdminVersionFirmwareMinor] != 3 ||
      data[kAdminVersionFirmwarePatch] != 4) {
    return Fail("firmware version fields are in the wrong place");
  }
  if (data[kAdminVersionProtocolMajor] != kAdminProtocolMajor ||
      data[kAdminVersionProtocolMinor] != kAdminProtocolMinor) {
    return Fail("admin protocol version fields are in the wrong place");
  }
  if (data[kAdminVersionCapabilities] != kAdminCapabilityVersionReport) {
    return Fail("capabilities field is in the wrong place");
  }
  if (ReadU32(&data[kAdminVersionBuildId]) != 0xDEADBEEF) {
    return Fail("build id is not where the report says it is");
  }
  // The build id is appended after the fields that existed before it, so a
  // reader written against the earlier layout still finds everything it knew.
  if (kAdminVersionBuildId <= kAdminVersionBoardType) {
    return Fail("the build id must come after the fields that predate it");
  }
  if (!VerifyCrc(frame, len)) return Fail("version report failed its own CRC");
  return Pass();
}

// The type field is the low nibble, so the space is small and worth watching.
Result CheckFrameTypeSpaceRemaining() {
  if (kFrameAdmin != 0x0E) return Fail("Admin must be 0x0E");
  // 0x0F is the only value left. If something takes it, the next protocol
  // addition has to go inside an existing envelope rather than beside it.
  const uint8_t typesInUse[] = {
      kFrameOutputState, kFrameSwitchState, kFrameHeartbeat, kFrameError,
      kFrameSetup, kFrameMapping, kFrameReset, kFrameConfig,
      kFrameSwitchNoChange, kFrameConfigAck, kFrameRestart, kFrameTrigger,
      kFrameSwitchRefresh, kFrameAdmin};
  for (uint8_t t : typesInUse) {
    if (t == 0x0F) return Fail("0x0F was the last free frame type and is gone");
    if ((t & 0xF0) != 0) return Fail("a frame type escaped the low nibble");
  }
  return Pass();
}

Result CheckUpdateBeginLayout() {
  uint8_t frame[kUpdateBeginFrameBytes];
  const size_t len = BuildUpdateBeginFrame(frame, /*boardId*/ 2, 9, 1,
                                           /*imageBytes*/ 0x00020034,
                                           /*crc*/ 0xBEEF);
  if (len != kUpdateBeginFrameBytes) return Fail("begin frame length wrong");
  if (frame[5] != kAdminUpdateBegin) return Fail("wrong admin command");
  if (frame[6] != 2) return Fail("begin must name its board");
  if (frame[7] != 0x00 || frame[8] != 0x02 || frame[9] != 0x00 ||
      frame[10] != 0x34) {
    return Fail("image size must be big-endian at offset 2 of the payload");
  }
  if (frame[11] != 0xBE || frame[12] != 0xEF) {
    return Fail("image CRC must be big-endian after the size");
  }
  if (!VerifyCrc(frame, len)) return Fail("begin frame failed its own CRC");

  uint32_t bytes = 0;
  uint16_t crc = 0;
  ReadUpdateBegin(frame + kHeaderBytes, bytes, crc);
  if (bytes != 0x00020034 || crc != 0xBEEF) {
    return Fail("begin payload did not round-trip");
  }
  return Pass();
}

Result CheckUpdateChunkLayout() {
  uint8_t frame[kUpdateChunkMaxFrameBytes];
  uint8_t data[kAdminChunkBytes];
  for (size_t i = 0; i < kAdminChunkBytes; ++i) {
    data[i] = static_cast<uint8_t>(i);
  }

  const size_t len = BuildUpdateChunkFrame(frame, /*boardId*/ 1, 3, 1,
                                           /*offset*/ 0x00010000, data,
                                           kAdminChunkBytes);
  if (len != kUpdateChunkMaxFrameBytes) return Fail("chunk frame length wrong");
  if (len != UpdateChunkFrameBytes(kAdminChunkBytes)) {
    return Fail("UpdateChunkFrameBytes disagrees with the builder");
  }
  if (frame[5] != kAdminUpdateChunk) return Fail("wrong admin command");

  uint32_t offset = 0;
  uint16_t length = 0;
  ReadUpdateChunkHead(frame + kHeaderBytes, offset, length);
  if (offset != 0x00010000 || length != kAdminChunkBytes) {
    return Fail("chunk head did not round-trip");
  }

  const uint8_t* body =
      frame + kHeaderBytes + kAdminPrefixBytes + kUpdateChunkHeadBytes;
  for (size_t i = 0; i < kAdminChunkBytes; ++i) {
    if (body[i] != data[i]) return Fail("chunk body did not round-trip");
  }
  if (!VerifyCrc(frame, len)) return Fail("chunk frame failed its own CRC");

  // A short final chunk must shrink the frame, not pad it.
  const size_t shortLen = BuildUpdateChunkFrame(frame, 1, 4, 1, 0, data, 7);
  if (shortLen != UpdateChunkFrameBytes(7)) {
    return Fail("a short chunk must produce a short frame");
  }
  if (!VerifyCrc(frame, shortLen)) return Fail("short chunk CRC wrong");
  return Pass();
}

Result CheckUpdateAckLayout() {
  uint8_t frame[kUpdateAckFrameBytes];
  const size_t len =
      BuildUpdateAckFrame(frame, kAdminUpdateChunkAck, /*boardId*/ 4, 2, 1,
                          kUpdateBadOffset, /*offset*/ 0x12345678);
  if (len != kUpdateAckFrameBytes) return Fail("ack frame length wrong");
  if (frame[5] != kAdminUpdateChunkAck) return Fail("wrong admin command");

  uint8_t status = 0;
  uint32_t offset = 0;
  ReadUpdateAck(frame + kHeaderBytes, status, offset);
  if (status != kUpdateBadOffset || offset != 0x12345678) {
    return Fail("ack payload did not round-trip");
  }
  if (!VerifyCrc(frame, len)) return Fail("ack frame failed its own CRC");
  return Pass();
}

Result CheckUpdateCommandsAreDistinct() {
  const uint8_t commands[] = {
      kAdminVersionQuery,   kAdminVersionReport,  kAdminUpdateBegin,
      kAdminUpdateBeginAck, kAdminUpdateChunk,    kAdminUpdateChunkAck,
      kAdminUpdateCommit,   kAdminUpdateResult};
  for (size_t i = 0; i < sizeof(commands); ++i) {
    if (commands[i] == 0) return Fail("0 is reserved as 'no command'");
    for (size_t j = i + 1; j < sizeof(commands); ++j) {
      if (commands[i] == commands[j]) {
        return Fail("two admin commands share a value: " + Num(commands[i]));
      }
    }
  }
  return Pass();
}

// A receive buffer sized to the largest runtime frame overruns on the first
// firmware chunk, which is five times larger. This asserts the constant the
// buffers are sized from actually covers every frame that can arrive.
Result CheckMaxFrameBytesCoversEverything() {
  RuntimeConfig maxed;
  maxed.coilBits = kMaxCoilBits;
  maxed.lampBits = kMaxLampBits;
  maxed.switchBits = kMaxSwitchBits;

  const size_t sizes[] = {
      kSetupFrameBytes,        kMappingFrameBytes,
      kConfigFrameBytes,       kConfigAckFrameBytes,
      kTriggerFrameBytes,      kResetFrameBytes,
      kAdminFrameBytes,        kUpdateBeginFrameBytes,
      kUpdateAckFrameBytes,    kUpdateCommitFrameBytes,
      kUpdateChunkMaxFrameBytes, OutputFrameBytes(maxed),
      SwitchFrameBytes(maxed), SwitchNoChangeFrameBytes(),
  };

  for (size_t size : sizes) {
    if (size > kMaxFrameBytes) {
      return Fail("a frame of " + Num(size) + " bytes exceeds kMaxFrameBytes " +
                  Num(kMaxFrameBytes));
    }
  }

  // And it should not be wildly larger than needed either, or every board
  // pays for it twice in RAM.
  if (kMaxFrameBytes != kUpdateChunkMaxFrameBytes) {
    return Fail("the firmware chunk should be the largest frame; "
                "kMaxFrameBytes = " + Num(kMaxFrameBytes));
  }
  return Pass();
}

// Board type decides which firmware image may be flashed to a board, so the
// values and names have to stay put: a renumbering silently pairs an image
// with the wrong hardware.
Result CheckBoardTypeValues() {
  const struct {
    uint8_t type;
    const char* name;
  } kExpected[] = {
      {kBoardTypeIo16_8_1, "IO_16_8_1"},
      {kBoardTypeIo16x8Matrix, "IO_16x8_matrix"},
      {kBoardTypeOut8x10, "Out_8x10"},
      {kBoardTypeOpto16, "Opto_16"},
  };

  for (const auto& e : kExpected) {
    const char* name = BoardTypeName(e.type);
    if (name == nullptr || std::string(name) != e.name) {
      return Fail(std::string("board type ") + Num(e.type) + " should be named " +
                  e.name);
    }
    if (BoardTypeFromName(e.name) != e.type) {
      return Fail(std::string(e.name) + " should map back to " + Num(e.type));
    }
  }

  // Anything unrecognised must not resolve to a real board, or an unfamiliar
  // filename could be flashed to whatever is on the bus.
  if (BoardTypeFromName("IO_16_8") != kBoardTypeUnknown ||
      BoardTypeFromName("IO_16_8_1x") != kBoardTypeUnknown ||
      BoardTypeFromName("") != kBoardTypeUnknown ||
      BoardTypeFromName(nullptr) != kBoardTypeUnknown) {
    return Fail("an unknown name must not resolve to a board type");
  }
  if (BoardTypeName(kBoardTypeUnknown) != nullptr) {
    return Fail("the unknown type must not have a name");
  }
  return Pass();
}

Result CheckVersionReportCarriesBoardType() {
  uint8_t frame[kAdminFrameBytes];
  BuildVersionReportFrame(frame, /*boardId*/ 1, 1, 1, 0, 2, 0,
                          kAdminCapabilityVersionReport, kBoardTypeOut8x10,
                          /*buildId*/ 0);
  const uint8_t* data = frame + kHeaderBytes + 2;
  if (data[kAdminVersionBoardType] != kBoardTypeOut8x10) {
    return Fail("the board type is not where the report says it is");
  }
  return Pass();
}

const Case kCases[] = {
    {"wire constants", CheckWireConstants},
    {"frame type values", CheckFrameTypeValues},
    {"BitsToBytes boundaries", CheckBitsToBytes},
    {"GI payload sizing", CheckGiPayloadSizing},
    {"defaults within maxima", CheckDefaultsWithinMaxima},
    {"frame sizes consistent", CheckFrameSizesAreConsistent},
    {"runtime sizing", CheckRuntimeSizing},
    {"runtime config validation", CheckRuntimeConfigValidation},
    {"CRC known answers", CheckCrcKnownAnswers},
    {"CRC detects corruption", CheckCrcDetectsCorruption},
    {"type/flag round trip", CheckTypeAndFlagRoundTrip},
    {"board validation", CheckBoardValidation},
    {"bitmap round trip", CheckBitmapRoundTrip},
    {"bitmap byte order", CheckBitmapByteOrder},
    {"GI nibble round trip", CheckGiNibbleRoundTrip},
    {"GI nibbles independent", CheckGiNibblesAreIndependent},
    {"GI clamping", CheckGiClamping},
    {"big-endian primitives", CheckBigEndianPrimitives},
    {"MappingFrame byte layout", CheckMappingFrameByteLayout},
    {"SetupFrame byte layout", CheckSetupFrameByteLayout},
    {"ConfigFrame byte layout", CheckConfigFrameByteLayout},
    {"header round trip", CheckHeaderRoundTrip},
    {"CRC rejects tampered frames", CheckCrcRejectsTamperedFrames},
    {"bare frame layout", CheckBareFrameLayout},
    {"TriggerFrame byte layout", CheckTriggerFrameLayout},
    {"AdminFrame byte layout", CheckAdminFrameLayout},
    {"version report layout", CheckVersionReportLayout},
    {"board type values and names", CheckBoardTypeValues},
    {"version report carries the board type", CheckVersionReportCarriesBoardType},
    {"frame type space", CheckFrameTypeSpaceRemaining},
    {"UpdateBegin byte layout", CheckUpdateBeginLayout},
    {"UpdateChunk byte layout", CheckUpdateChunkLayout},
    {"UpdateAck byte layout", CheckUpdateAckLayout},
    {"admin commands are distinct", CheckUpdateCommandsAreDistinct},
    {"kMaxFrameBytes covers every frame", CheckMaxFrameBytesCoversEverything},
    {"switch reply layout", CheckSwitchReplyLayout},
    {"OutputState layout", CheckOutputStateLayout},
    {"wire sizes are not struct padding", CheckWireSizesAreNotStructPadding},
    {"codec is endian independent", CheckCodecIsEndianIndependent},
};

const size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

}  // namespace ppuc_conformance
