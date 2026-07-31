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
  if (frame[1] != (kFrameMapping | kFlagKeyframe)) {
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
  if (frame[1] != (kFrameSetup | kFlagKeyframe)) return Fail("setup type byte wrong");
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
}  // namespace

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
};

const size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

}  // namespace ppuc_conformance
