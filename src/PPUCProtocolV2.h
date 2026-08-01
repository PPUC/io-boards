#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ppuc {
namespace v2 {

constexpr uint32_t kBaudRate = 115200;

constexpr uint8_t kSyncByte = 0xA5;
constexpr uint8_t kNoBoard = 0xFF;
constexpr uint8_t kMaxBoards = 8;
constexpr uint8_t kGiStrings = 5;
constexpr uint8_t kMaxGiLevel = 8;
constexpr uint8_t kGiLevelBits = 4;

// Bitmaps are indexed by global device number: bit N => device number N.
// Runtime counts are configured per game and announced with SetupFrame.
constexpr uint16_t kDefaultCoilBits = 24;
constexpr uint16_t kDefaultLampBits = 64;
constexpr uint16_t kDefaultSwitchBits = 64;
constexpr uint16_t kMaxCoilBits = 64;
constexpr uint16_t kMaxLampBits = 256;
constexpr uint16_t kMaxSwitchBits = 256;

constexpr size_t BitsToBytes(uint16_t bits) { return (bits + 7u) / 8u; }

constexpr size_t kDefaultCoilBytes = BitsToBytes(kDefaultCoilBits);
constexpr size_t kDefaultLampBytes = BitsToBytes(kDefaultLampBits);
constexpr size_t kDefaultSwitchBytes = BitsToBytes(kDefaultSwitchBits);
constexpr size_t kMaxCoilBytes = BitsToBytes(kMaxCoilBits);
constexpr size_t kMaxLampBytes = BitsToBytes(kMaxLampBits);
constexpr size_t kMaxSwitchBytes = BitsToBytes(kMaxSwitchBits);
constexpr size_t kGiBytes = (kGiStrings * kGiLevelBits + 7u) / 8u;

constexpr size_t kHeaderBytes = 5;
constexpr size_t kCrcBytes = 2;

enum FrameType : uint8_t {
  kFrameOutputState = 0x01,
  kFrameSwitchState = 0x02,
  kFrameHeartbeat = 0x03,
  kFrameError = 0x04,
  kFrameSetup = 0x05,
  kFrameMapping = 0x06,
  kFrameReset = 0x07,
  kFrameConfig = 0x08,
  kFrameSwitchNoChange = 0x09,
  kFrameConfigAck = 0x0A,
  kFrameRestart = 0x0B,
  kFrameTrigger = 0x0C,
  kFrameSwitchRefresh = 0x0D,
};

enum MappingDomain : uint8_t {
  kDomainCoil = 0x01,
  kDomainLamp = 0x02,
  kDomainSwitch = 0x03,
};

enum FrameFlag : uint8_t {
  kFlagNone = 0x00,
  kFlagKeyframe = 0x10,
  kFlagDelta = 0x20,
  kFlagError = 0x80,
};

enum SwitchStatusFlag : uint8_t {
  kStatusInSync = 0x01,
  kStatusNeedsSetup = 0x02,
  kStatusMappingIncomplete = 0x04,
  kStatusSequenceGap = 0x08,
  kStatusParserResynced = 0x10,
  kStatusSwitchOverflow = 0x20,
};

enum ConfigAckStatus : uint8_t {
  kConfigAckAccepted = 0x00,
  kConfigAckRejected = 0x01,
};

struct FrameHeader {
  uint8_t sync;
  uint8_t typeAndFlags;
  uint8_t nextBoard;
  uint8_t sequence;
  uint8_t epoch;
};

struct SetupPayload {
  uint16_t coilBits;
  uint16_t lampBits;
  uint16_t switchBits;
};

struct MappingPayload {
  uint8_t domain;
  uint8_t reserved;
  uint16_t index;
  uint16_t number;
};

struct ConfigPayload {
  uint8_t boardId;
  uint8_t topic;
  uint8_t index;
  uint8_t key;
  uint32_t value;
};

struct OutputPayload {
  // Only first BitsToBytes(coilBits/lampBits) bytes are used at runtime.
  uint8_t coils[kMaxCoilBytes];
  uint8_t lamps[kMaxLampBytes];
  uint8_t gi[kGiBytes];
};

struct ConfigAckPayload {
  uint8_t boardId;
  uint8_t topic;
  uint8_t index;
  uint8_t key;
  uint8_t status;
  uint8_t reserved[3];
};

struct SwitchPayload {
  uint8_t epochSeen;
  uint8_t lastHostSequenceSeen;
  uint8_t statusFlags;
  uint8_t reserved;
  // Only first BitsToBytes(switchBits) bytes are used at runtime.
  uint8_t switches[kMaxSwitchBytes];
};

struct TriggerPayload {
  uint8_t source;
  uint8_t numberHi;
  uint8_t numberLow;
  uint8_t value;
};

struct SetupFrame {
  FrameHeader header;
  SetupPayload payload;
  uint16_t crc;
};

struct MappingFrame {
  FrameHeader header;
  MappingPayload payload;
  uint16_t crc;
};

struct ConfigFrame {
  FrameHeader header;
  ConfigPayload payload;
  uint16_t crc;
};

struct OutputStateFrame {
  FrameHeader header;
  OutputPayload payload;
  uint16_t crc;
};

struct ConfigAckFrame {
  FrameHeader header;
  ConfigAckPayload payload;
  uint16_t crc;
};

struct SwitchStateFrame {
  FrameHeader header;
  SwitchPayload payload;
  uint16_t crc;
};

struct TriggerFrame {
  FrameHeader header;
  TriggerPayload payload;
  uint16_t crc;
};

constexpr size_t kSetupPayloadBytes = sizeof(SetupPayload);
constexpr size_t kMappingPayloadBytes = sizeof(MappingPayload);
constexpr size_t kConfigPayloadBytes = sizeof(ConfigPayload);
constexpr size_t kOutputPayloadBytes = sizeof(OutputPayload);
constexpr size_t kConfigAckPayloadBytes = sizeof(ConfigAckPayload);
constexpr size_t kSwitchPayloadBytes = sizeof(SwitchPayload);
constexpr size_t kTriggerPayloadBytes = sizeof(TriggerPayload);
constexpr size_t kSwitchStatusBytes = 4;
constexpr size_t kResetFrameBytes = kHeaderBytes + kCrcBytes;
constexpr size_t kRestartFrameBytes = kHeaderBytes + kCrcBytes;
constexpr size_t kSwitchRefreshFrameBytes = kHeaderBytes + kCrcBytes;
constexpr size_t kSetupFrameBytes = kHeaderBytes + kSetupPayloadBytes + kCrcBytes;
constexpr size_t kMappingFrameBytes = kHeaderBytes + kMappingPayloadBytes + kCrcBytes;
constexpr size_t kConfigFrameBytes = kHeaderBytes + kConfigPayloadBytes + kCrcBytes;
constexpr size_t kOutputFrameBytes = kHeaderBytes + kOutputPayloadBytes + kCrcBytes;
constexpr size_t kConfigAckFrameBytes = kHeaderBytes + kConfigAckPayloadBytes + kCrcBytes;
constexpr size_t kSwitchFrameBytes = kHeaderBytes + kSwitchPayloadBytes + kCrcBytes;
constexpr size_t kTriggerFrameBytes = kHeaderBytes + kTriggerPayloadBytes + kCrcBytes;

// The wire format is deliberately assembled byte at a time, never by copying a
// struct, so that host endianness, struct padding and alignment cannot affect
// it. The structs above describe the layout for readers; they are not
// serialized.
//
// The frame size constants are nevertheless derived from sizeof() on those
// structs. If a compiler or ABI padded one, the frame *length* would change
// while the layout did not, and frames would go out the wrong size. These
// assertions pin the sizes to their wire values so that fails at compile time
// on the offending platform rather than silently on the bus.
static_assert(sizeof(FrameHeader) == 5, "FrameHeader must be 5 bytes on the wire");
static_assert(kSetupPayloadBytes == 6, "SetupPayload must be 6 bytes on the wire");
static_assert(kMappingPayloadBytes == 6, "MappingPayload must be 6 bytes on the wire");
static_assert(kConfigPayloadBytes == 8, "ConfigPayload must be 8 bytes on the wire");
static_assert(kConfigAckPayloadBytes == 8, "ConfigAckPayload must be 8 bytes on the wire");
static_assert(kTriggerPayloadBytes == 4, "TriggerPayload must be 4 bytes on the wire");
static_assert(kSwitchStatusBytes == 4, "the switch status prefix is 4 bytes on the wire");
static_assert(kGiBytes == 3, "5 GI strings at 4 bits each pack into 3 bytes");

struct RuntimeConfig {
  uint16_t coilBits = kDefaultCoilBits;
  uint16_t lampBits = kDefaultLampBits;
  uint16_t switchBits = kDefaultSwitchBits;
};

inline bool IsValidRuntimeConfig(const RuntimeConfig& cfg) {
  return cfg.coilBits > 0 && cfg.coilBits <= kMaxCoilBits && cfg.lampBits > 0 &&
         cfg.lampBits <= kMaxLampBits && cfg.switchBits > 0 &&
         cfg.switchBits <= kMaxSwitchBits;
}

inline size_t OutputPayloadBytes(const RuntimeConfig& cfg) {
  return BitsToBytes(cfg.coilBits) + BitsToBytes(cfg.lampBits) + kGiBytes;
}

inline size_t SwitchPayloadBytes(const RuntimeConfig& cfg) {
  return kSwitchStatusBytes + BitsToBytes(cfg.switchBits);
}

inline size_t SwitchNoChangePayloadBytes() { return kSwitchStatusBytes; }

inline size_t SwitchNoChangeFrameBytes() {
  return kHeaderBytes + SwitchNoChangePayloadBytes() + kCrcBytes;
}

inline size_t OutputFrameBytes(const RuntimeConfig& cfg) {
  return kHeaderBytes + OutputPayloadBytes(cfg) + kCrcBytes;
}

inline size_t SwitchFrameBytes(const RuntimeConfig& cfg) {
  return kHeaderBytes + SwitchPayloadBytes(cfg) + kCrcBytes;
}

inline uint16_t Crc16Ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

inline uint8_t ComposeTypeAndFlags(FrameType type, uint8_t flags) {
  return static_cast<uint8_t>(static_cast<uint8_t>(type) | flags);
}

inline FrameType ExtractType(uint8_t typeAndFlags) {
  return static_cast<FrameType>(typeAndFlags & 0x0F);
}

inline uint8_t ExtractFlags(uint8_t typeAndFlags) {
  return static_cast<uint8_t>(typeAndFlags & 0xF0);
}

inline bool IsValidBoard(uint8_t board) {
  return board == kNoBoard || board < kMaxBoards;
}

inline void SetBitmapBit(uint8_t* bitmap, uint16_t number, bool on) {
  const uint16_t byteIndex = number / 8u;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (number % 8u));
  if (on) {
    bitmap[byteIndex] |= bitMask;
  } else {
    bitmap[byteIndex] &= static_cast<uint8_t>(~bitMask);
  }
}

inline bool GetBitmapBit(const uint8_t* bitmap, uint16_t number) {
  const uint16_t byteIndex = number / 8u;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (number % 8u));
  return (bitmap[byteIndex] & bitMask) != 0;
}

inline uint8_t ClampGiLevel(uint8_t level) {
  return level > kMaxGiLevel ? kMaxGiLevel : level;
}

inline void SetPackedNibble(uint8_t* data, uint8_t index, uint8_t value) {
  const uint8_t clampedValue = static_cast<uint8_t>(value & 0x0F);
  const uint8_t byteIndex = index / 2u;
  if ((index & 1u) == 0) {
    data[byteIndex] =
        static_cast<uint8_t>((data[byteIndex] & 0x0F) | (clampedValue << 4));
  } else {
    data[byteIndex] =
        static_cast<uint8_t>((data[byteIndex] & 0xF0) | clampedValue);
  }
}

// ---------------------------------------------------------------------------
// Wire codec
// ---------------------------------------------------------------------------
//
// Every multi-byte field on the bus is big-endian, and until now both sides
// open-coded that: libppuc shifted bytes into a buffer by hand, the firmware
// reassembled them with word() by hand. Two hand-written implementations of one
// format is exactly where drift happens, and no test could catch the two
// disagreeing because neither shared any code.
//
// These helpers are the single definition. They are header-only and operate on
// caller-provided buffers, so they add no allocation, no linkage and nothing
// the RP2040 cannot afford.
//
// Layout, for reference:
//   [0] sync  [1] typeAndFlags  [2] nextBoard  [3] sequence  [4] epoch
//   [5..]     payload
//   [last-1], [last]  CRC-16/CCITT over header + payload, big-endian

inline void WriteU16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[1] = static_cast<uint8_t>(value & 0xFF);
}

inline uint16_t ReadU16(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline void WriteU32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[3] = static_cast<uint8_t>(value & 0xFF);
}

inline uint32_t ReadU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void WriteHeader(uint8_t* frame, FrameType type, uint8_t flags,
                        uint8_t nextBoard, uint8_t sequence, uint8_t epoch) {
  frame[0] = kSyncByte;
  frame[1] = ComposeTypeAndFlags(type, flags);
  frame[2] = nextBoard;
  frame[3] = sequence;
  frame[4] = epoch;
}

// Returns false when the sync byte is wrong, which is the caller's cue to
// resynchronise rather than trust the rest of the buffer.
inline bool ReadHeader(const uint8_t* frame, FrameHeader& out) {
  if (frame[0] != kSyncByte) {
    return false;
  }
  out.sync = frame[0];
  out.typeAndFlags = frame[1];
  out.nextBoard = frame[2];
  out.sequence = frame[3];
  out.epoch = frame[4];
  return true;
}

// Appends the CRC over header + payload. Returns the total frame length, so a
// caller can write `const size_t len = AppendCrc(buf, kHeaderBytes + payload);`
inline size_t AppendCrc(uint8_t* frame, size_t headerAndPayloadBytes) {
  const uint16_t crc = Crc16Ccitt(frame, headerAndPayloadBytes);
  WriteU16(frame + headerAndPayloadBytes, crc);
  return headerAndPayloadBytes + kCrcBytes;
}

// Verifies a complete frame, CRC included. `totalBytes` counts the CRC.
inline bool VerifyCrc(const uint8_t* frame, size_t totalBytes) {
  if (totalBytes < kHeaderBytes + kCrcBytes) {
    return false;
  }
  const size_t bodyBytes = totalBytes - kCrcBytes;
  return ReadU16(frame + bodyBytes) == Crc16Ccitt(frame, bodyBytes);
}

// --- payloads with multi-byte fields ---------------------------------------

inline void WriteSetupPayload(uint8_t* payload, const RuntimeConfig& cfg) {
  WriteU16(payload, cfg.coilBits);
  WriteU16(payload + 2, cfg.lampBits);
  WriteU16(payload + 4, cfg.switchBits);
}

inline void ReadSetupPayload(const uint8_t* payload, RuntimeConfig& cfg) {
  cfg.coilBits = ReadU16(payload);
  cfg.lampBits = ReadU16(payload + 2);
  cfg.switchBits = ReadU16(payload + 4);
}

inline void WriteMappingPayload(uint8_t* payload, uint8_t domain, uint16_t index,
                                uint16_t number) {
  payload[0] = domain;
  payload[1] = 0;  // reserved
  WriteU16(payload + 2, index);
  WriteU16(payload + 4, number);
}

inline void ReadMappingPayload(const uint8_t* payload, uint8_t& domain,
                               uint16_t& index, uint16_t& number) {
  domain = payload[0];
  index = ReadU16(payload + 2);
  number = ReadU16(payload + 4);
}

inline void WriteConfigPayload(uint8_t* payload, uint8_t boardId, uint8_t topic,
                               uint8_t index, uint8_t key, uint32_t value) {
  payload[0] = boardId;
  payload[1] = topic;
  payload[2] = index;
  payload[3] = key;
  WriteU32(payload + 4, value);
}

inline void ReadConfigPayload(const uint8_t* payload, uint8_t& boardId,
                              uint8_t& topic, uint8_t& index, uint8_t& key,
                              uint32_t& value) {
  boardId = payload[0];
  topic = payload[1];
  index = payload[2];
  key = payload[3];
  value = ReadU32(payload + 4);
}

// --- whole-frame builders ---------------------------------------------------
// Each returns the total frame length including CRC.

inline size_t BuildSetupFrame(uint8_t* frame, uint8_t nextBoard,
                              uint8_t sequence, uint8_t epoch,
                              const RuntimeConfig& cfg) {
  WriteHeader(frame, kFrameSetup, kFlagKeyframe, nextBoard, sequence, epoch);
  WriteSetupPayload(frame + kHeaderBytes, cfg);
  return AppendCrc(frame, kHeaderBytes + kSetupPayloadBytes);
}

inline size_t BuildMappingFrame(uint8_t* frame, uint8_t nextBoard,
                                uint8_t sequence, uint8_t epoch, uint8_t domain,
                                uint16_t index, uint16_t number) {
  WriteHeader(frame, kFrameMapping, kFlagKeyframe, nextBoard, sequence, epoch);
  WriteMappingPayload(frame + kHeaderBytes, domain, index, number);
  return AppendCrc(frame, kHeaderBytes + kMappingPayloadBytes);
}

inline size_t BuildConfigFrame(uint8_t* frame, uint8_t nextBoard,
                               uint8_t sequence, uint8_t epoch, uint8_t boardId,
                               uint8_t topic, uint8_t index, uint8_t key,
                               uint32_t value) {
  WriteHeader(frame, kFrameConfig, kFlagKeyframe, nextBoard, sequence, epoch);
  WriteConfigPayload(frame + kHeaderBytes, boardId, topic, index, key, value);
  return AppendCrc(frame, kHeaderBytes + kConfigPayloadBytes);
}

inline void WriteConfigAckPayload(uint8_t* payload, uint8_t boardId,
                                  uint8_t topic, uint8_t index, uint8_t key,
                                  uint8_t status) {
  payload[0] = boardId;
  payload[1] = topic;
  payload[2] = index;
  payload[3] = key;
  payload[4] = status;
  payload[5] = 0;  // reserved
  payload[6] = 0;
  payload[7] = 0;
}

inline void ReadConfigAckPayload(const uint8_t* payload, uint8_t& boardId,
                                 uint8_t& topic, uint8_t& index, uint8_t& key,
                                 uint8_t& status) {
  boardId = payload[0];
  topic = payload[1];
  index = payload[2];
  key = payload[3];
  status = payload[4];
}

inline size_t BuildConfigAckFrame(uint8_t* frame, uint8_t nextBoard,
                                  uint8_t sequence, uint8_t epoch,
                                  uint8_t boardId, uint8_t topic, uint8_t index,
                                  uint8_t key, uint8_t status) {
  WriteHeader(frame, kFrameConfigAck, kFlagNone, nextBoard, sequence, epoch);
  WriteConfigAckPayload(frame + kHeaderBytes, boardId, topic, index, key,
                        status);
  return AppendCrc(frame, kHeaderBytes + kConfigAckPayloadBytes);
}

// Reset, Restart and SwitchRefresh carry no payload.
inline size_t BuildBareFrame(uint8_t* frame, FrameType type, uint8_t flags,
                             uint8_t nextBoard, uint8_t sequence,
                             uint8_t epoch) {
  WriteHeader(frame, type, flags, nextBoard, sequence, epoch);
  return AppendCrc(frame, kHeaderBytes);
}

inline void WriteTriggerPayload(uint8_t* payload, uint8_t source,
                                uint16_t number, uint8_t value) {
  payload[0] = source;
  WriteU16(payload + 1, number);
  payload[3] = value;
}

inline void ReadTriggerPayload(const uint8_t* payload, uint8_t& source,
                               uint16_t& number, uint8_t& value) {
  source = payload[0];
  number = ReadU16(payload + 1);
  value = payload[3];
}

inline size_t BuildTriggerFrame(uint8_t* frame, uint8_t nextBoard,
                                uint8_t sequence, uint8_t epoch, uint8_t source,
                                uint16_t number, uint8_t value) {
  WriteHeader(frame, kFrameTrigger, kFlagNone, nextBoard, sequence, epoch);
  WriteTriggerPayload(frame + kHeaderBytes, source, number, value);
  return AppendCrc(frame, kHeaderBytes + kTriggerPayloadBytes);
}

// The switch status prefix carried by both switch reply variants.
inline void WriteSwitchStatus(uint8_t* payload, uint8_t epochSeen,
                              uint8_t lastHostSequenceSeen, uint8_t flags) {
  payload[0] = epochSeen;
  payload[1] = lastHostSequenceSeen;
  payload[2] = flags;
  payload[3] = 0;  // reserved
}

inline void ReadSwitchStatus(const uint8_t* payload, uint8_t& epochSeen,
                             uint8_t& lastHostSequenceSeen, uint8_t& flags) {
  epochSeen = payload[0];
  lastHostSequenceSeen = payload[1];
  flags = payload[2];
}

// A SwitchNoChange reply is the status prefix alone; a SwitchState reply
// appends the switch bitmap. Pass switchBytes == 0 for no-change.
inline size_t BuildSwitchReplyFrame(uint8_t* frame, bool sendState,
                                    uint8_t nextBoard, uint8_t sequence,
                                    uint8_t epoch, uint8_t epochSeen,
                                    uint8_t lastHostSequenceSeen, uint8_t flags,
                                    const uint8_t* switchBitmap,
                                    size_t switchBytes) {
  WriteHeader(frame, sendState ? kFrameSwitchState : kFrameSwitchNoChange,
              sendState ? kFlagKeyframe : kFlagNone, nextBoard, sequence,
              epoch);
  WriteSwitchStatus(frame + kHeaderBytes, epochSeen, lastHostSequenceSeen,
                    flags);

  size_t payloadBytes = kSwitchStatusBytes;
  if (sendState && switchBitmap != nullptr) {
    for (size_t i = 0; i < switchBytes; ++i) {
      frame[kHeaderBytes + kSwitchStatusBytes + i] = switchBitmap[i];
    }
    payloadBytes += switchBytes;
  }
  return AppendCrc(frame, kHeaderBytes + payloadBytes);
}

// OutputState is variable length: dense coil bitmap, dense lamp bitmap, then
// packed GI levels. giLevels is one byte per string, clamped on the way in.
inline void WriteOutputPayload(uint8_t* payload, const RuntimeConfig& cfg,
                               const uint8_t* coils, const uint8_t* lamps,
                               const uint8_t* giLevels) {
  const size_t coilBytes = BitsToBytes(cfg.coilBits);
  const size_t lampBytes = BitsToBytes(cfg.lampBits);

  for (size_t i = 0; i < coilBytes; ++i) payload[i] = coils[i];
  for (size_t i = 0; i < lampBytes; ++i) payload[coilBytes + i] = lamps[i];

  uint8_t* gi = payload + coilBytes + lampBytes;
  for (size_t i = 0; i < kGiBytes; ++i) gi[i] = 0;
  for (uint8_t s = 0; s < kGiStrings; ++s) {
    SetPackedNibble(gi, s, ClampGiLevel(giLevels[s]));
  }
}

inline size_t BuildOutputStateFrame(uint8_t* frame, uint8_t nextBoard,
                                    uint8_t sequence, uint8_t epoch,
                                    const RuntimeConfig& cfg,
                                    const uint8_t* coils, const uint8_t* lamps,
                                    const uint8_t* giLevels) {
  WriteHeader(frame, kFrameOutputState, kFlagKeyframe, nextBoard, sequence,
              epoch);
  WriteOutputPayload(frame + kHeaderBytes, cfg, coils, lamps, giLevels);
  return AppendCrc(frame, kHeaderBytes + OutputPayloadBytes(cfg));
}

inline uint8_t GetPackedNibble(const uint8_t* data, uint8_t index) {
  const uint8_t byteIndex = index / 2u;
  if ((index & 1u) == 0) {
    return static_cast<uint8_t>((data[byteIndex] >> 4) & 0x0F);
  }
  return static_cast<uint8_t>(data[byteIndex] & 0x0F);
}

}  // namespace v2
}  // namespace ppuc
