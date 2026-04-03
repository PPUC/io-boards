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

struct ConfigAckPayload {
  uint8_t boardId;
  uint8_t topic;
  uint8_t index;
  uint8_t key;
  uint8_t status;
  uint8_t reserved[3];
};

struct OutputPayload {
  // Only first BitsToBytes(coilBits/lampBits) bytes are used at runtime.
  uint8_t coils[kMaxCoilBytes];
  uint8_t lamps[kMaxLampBytes];
  uint8_t gi[kGiBytes];
};

struct SwitchPayload {
  uint8_t epochSeen;
  uint8_t lastHostSequenceSeen;
  uint8_t statusFlags;
  uint8_t reserved;
  // Only first BitsToBytes(switchBits) bytes are used at runtime.
  uint8_t switches[kMaxSwitchBytes];
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

struct ConfigAckFrame {
  FrameHeader header;
  ConfigAckPayload payload;
  uint16_t crc;
};

struct OutputStateFrame {
  FrameHeader header;
  OutputPayload payload;
  uint16_t crc;
};

struct SwitchStateFrame {
  FrameHeader header;
  SwitchPayload payload;
  uint16_t crc;
};

constexpr size_t kSetupPayloadBytes = sizeof(SetupPayload);
constexpr size_t kMappingPayloadBytes = sizeof(MappingPayload);
constexpr size_t kConfigPayloadBytes = sizeof(ConfigPayload);
constexpr size_t kConfigAckPayloadBytes = sizeof(ConfigAckPayload);
constexpr size_t kOutputPayloadBytes = sizeof(OutputPayload);
constexpr size_t kSwitchPayloadBytes = sizeof(SwitchPayload);
constexpr size_t kSwitchStatusBytes = 4;
constexpr size_t kResetFrameBytes = kHeaderBytes + kCrcBytes;
constexpr size_t kSetupFrameBytes = kHeaderBytes + kSetupPayloadBytes + kCrcBytes;
constexpr size_t kMappingFrameBytes = kHeaderBytes + kMappingPayloadBytes + kCrcBytes;
constexpr size_t kConfigFrameBytes = kHeaderBytes + kConfigPayloadBytes + kCrcBytes;
constexpr size_t kConfigAckFrameBytes = kHeaderBytes + kConfigAckPayloadBytes + kCrcBytes;
constexpr size_t kOutputFrameBytes = kHeaderBytes + kOutputPayloadBytes + kCrcBytes;
constexpr size_t kSwitchFrameBytes = kHeaderBytes + kSwitchPayloadBytes + kCrcBytes;

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

inline uint8_t GetPackedNibble(const uint8_t* data, uint8_t index) {
  const uint8_t byteIndex = index / 2u;
  if ((index & 1u) == 0) {
    return static_cast<uint8_t>((data[byteIndex] >> 4) & 0x0F);
  }
  return static_cast<uint8_t>(data[byteIndex] & 0x0F);
}

}  // namespace v2
}  // namespace ppuc
