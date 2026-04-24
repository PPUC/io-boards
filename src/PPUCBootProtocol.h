#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ppuc {
namespace boot {

constexpr uint8_t kSyncByte = 0x5C;
constexpr uint8_t kProtocolMajor = 1;
constexpr uint8_t kProtocolMinor = 0;

constexpr size_t kHeaderBytes = 4;
constexpr size_t kCrcBytes = 2;

enum FrameType : uint8_t {
  kFrameHello = 0x01,
  kFrameHelloAck = 0x02,
};

enum Intent : uint8_t {
  kIntentQuery = 0x00,
  kIntentRuntime = 0x01,
  kIntentUpdate = 0x02,
};

enum Mode : uint8_t {
  kModeAwaitingCommand = 0x00,
  kModeRuntimeSelected = 0x01,
  kModeUpdateSelected = 0x02,
};

enum Capability : uint8_t {
  kCapabilityRuntimeCommand = 0x01,
  kCapabilityUpdateCommand = 0x02,
};

struct FrameHeader {
  uint8_t sync;
  uint8_t type;
  uint8_t boardId;
  uint8_t sequence;
};

struct HelloPayload {
  uint8_t intent;
  uint8_t protocolMajor;
  uint8_t protocolMinor;
  uint8_t reserved;
};

struct HelloAckPayload {
  uint8_t boardId;
  uint8_t mode;
  uint8_t firmwareMajor;
  uint8_t firmwareMinor;
  uint8_t firmwarePatch;
  uint8_t protocolMajor;
  uint8_t protocolMinor;
  uint8_t capabilities;
};

constexpr size_t kHelloPayloadBytes = sizeof(HelloPayload);
constexpr size_t kHelloAckPayloadBytes = sizeof(HelloAckPayload);
constexpr size_t kHelloFrameBytes = kHeaderBytes + kHelloPayloadBytes + kCrcBytes;
constexpr size_t kHelloAckFrameBytes =
    kHeaderBytes + kHelloAckPayloadBytes + kCrcBytes;

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

}  // namespace boot
}  // namespace ppuc
