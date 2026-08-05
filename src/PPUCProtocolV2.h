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
  // Envelope for board administration: version reporting, run-mode selection
  // and, later, firmware transfer.
  //
  // One type rather than one per message, because the type field is the low
  // nibble of typeAndFlags and only 0x0E and 0x0F were left. Administration
  // needs more messages than that, so the command lives in the payload and
  // 0x0F stays available. Reusing this framing also means administration
  // inherits the CRC, sequence, epoch and parser rather than duplicating them.
  kFrameAdmin = 0x0E,
};

// Sub-commands carried in AdminPayload.command.
enum AdminCommand : uint8_t {
  kAdminVersionQuery = 0x01,   // host -> board
  kAdminVersionReport = 0x02,  // board -> host
  kAdminUpdateBegin = 0x03,    // host -> board: image size and CRC
  kAdminUpdateBeginAck = 0x04, // board -> host: staging ready, or refused
  kAdminUpdateChunk = 0x05,    // host -> board: offset + image bytes
  kAdminUpdateChunkAck = 0x06, // board -> host: offset accepted, or error
  kAdminUpdateCommit = 0x07,   // host -> board: verify and install
  kAdminUpdateResult = 0x08,   // board -> host: outcome
};

// Why a board refused, or how an update ended. Reported rather than inferred
// from silence, so a refusal is distinguishable from a board that is simply
// not answering.
enum AdminUpdateStatus : uint8_t {
  kUpdateOk = 0x00,
  kUpdateBusy = 0x01,           // an update is already in progress
  kUpdateTooLarge = 0x02,       // image will not fit the staging area
  kUpdateBadOffset = 0x03,      // chunk arrived out of order or out of range
  kUpdateCrcMismatch = 0x04,    // staged image does not match the announced CRC
  kUpdateNotStaged = 0x05,      // commit without a complete transfer
  kUpdateWriteFailed = 0x06,    // flash refused the write
  kUpdateUnsupported = 0x07,    // this board cannot self-update
};

// One chunk carries exactly one UF2 block's payload, which is what the image
// is made of, so no repacking is needed on either side.
constexpr size_t kAdminChunkBytes = 256;

// Version of the administration contract itself, independent of the frame
// format. A board reports it so a host can tell what it is able to ask for.
constexpr uint8_t kAdminProtocolMajor = 1;
constexpr uint8_t kAdminProtocolMinor = 0;

// Which board this firmware is built for.
//
// On the wire because firmware is board-specific: the same GPIO is a coil
// output on one board and an input on another, so an image flashed to the
// wrong type does not merely misbehave, it drives outputs into inputs. A
// board reports its type so the host can refuse the mismatch rather than
// discover it electrically.
//
// Values are on the wire and must not be renumbered. Names match the
// PlatformIO environments and the CI artefact names, which is also how images
// are matched to boards on disk.
enum BoardType : uint8_t {
  kBoardTypeUnknown = 0x00,
  kBoardTypeIo16_8_1 = 0x01,     // IO_16_8_1
  kBoardTypeIo16x8Matrix = 0x02, // IO_16x8_matrix
  kBoardTypeOut8x10 = 0x03,      // Out_8x10
  kBoardTypeOpto16 = 0x04,       // Opto_16
};

// Canonical name for a board type, or nullptr if unknown. One definition so
// firmware filenames, config-tool board names and the wire value cannot drift
// apart.
constexpr const char* BoardTypeName(uint8_t type) {
  switch (type) {
    case kBoardTypeIo16_8_1: return "IO_16_8_1";
    case kBoardTypeIo16x8Matrix: return "IO_16x8_matrix";
    case kBoardTypeOut8x10: return "Out_8x10";
    case kBoardTypeOpto16: return "Opto_16";
    default: return nullptr;
  }
}

// Inverse of BoardTypeName. Returns kBoardTypeUnknown for anything unrecognised
// rather than guessing, so an unfamiliar filename cannot be flashed to
// whatever happens to be on the bus.
constexpr uint8_t BoardTypeFromName(const char* name) {
  if (name == nullptr) {
    return kBoardTypeUnknown;
  }
  for (uint8_t type = kBoardTypeIo16_8_1; type <= kBoardTypeOpto16; ++type) {
    const char* candidate = BoardTypeName(type);
    if (candidate == nullptr) {
      continue;
    }
    const char* a = candidate;
    const char* b = name;
    while (*a != '\0' && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == '\0' && *b == '\0') {
      return type;
    }
  }
  return kBoardTypeUnknown;
}

// What a board is willing to do, reported in a version report.
enum AdminCapability : uint8_t {
  kAdminCapabilityVersionReport = 0x01,
  kAdminCapabilityFirmwareUpdate = 0x02,
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

// Administration envelope.
//
// boardId is explicit rather than inferred from token order: administration
// happens before and outside the switch chain, where there is no token to
// infer from. A board answers only when the id matches its own, so a host that
// polls one board at a time cannot cause two boards to reply at once.
constexpr size_t kAdminDataBytes = 8;
struct AdminPayload {
  uint8_t command;
  uint8_t boardId;
  uint8_t data[kAdminDataBytes];
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
constexpr size_t kAdminPayloadBytes = sizeof(AdminPayload);
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
constexpr size_t kAdminFrameBytes = kHeaderBytes + kAdminPayloadBytes + kCrcBytes;

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
static_assert(kAdminPayloadBytes == 10, "AdminPayload must be 10 bytes on the wire");
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

inline void WriteAdminPayload(uint8_t* payload, uint8_t command,
                              uint8_t boardId, const uint8_t* data) {
  payload[0] = command;
  payload[1] = boardId;
  for (size_t i = 0; i < kAdminDataBytes; ++i) {
    payload[2 + i] = data ? data[i] : 0;
  }
}

inline void ReadAdminPayload(const uint8_t* payload, uint8_t& command,
                             uint8_t& boardId, uint8_t* data) {
  command = payload[0];
  boardId = payload[1];
  if (data) {
    for (size_t i = 0; i < kAdminDataBytes; ++i) {
      data[i] = payload[2 + i];
    }
  }
}

inline size_t BuildAdminFrame(uint8_t* frame, uint8_t command, uint8_t boardId,
                              uint8_t nextBoard, uint8_t sequence,
                              uint8_t epoch, const uint8_t* data) {
  WriteHeader(frame, kFrameAdmin, kFlagNone, nextBoard, sequence, epoch);
  WriteAdminPayload(frame + kHeaderBytes, command, boardId, data);
  return AppendCrc(frame, kHeaderBytes + kAdminPayloadBytes);
}

// Layout of the data area for kAdminVersionReport. Named so both sides index
// it the same way rather than each counting offsets by hand.
enum AdminVersionField : uint8_t {
  kAdminVersionFirmwareMajor = 0,
  kAdminVersionFirmwareMinor = 1,
  kAdminVersionFirmwarePatch = 2,
  kAdminVersionProtocolMajor = 3,
  kAdminVersionProtocolMinor = 4,
  kAdminVersionCapabilities = 5,
  kAdminVersionBoardType = 6,
  // 7 reserved
};

inline size_t BuildVersionQueryFrame(uint8_t* frame, uint8_t boardId,
                                     uint8_t sequence, uint8_t epoch) {
  const uint8_t data[kAdminDataBytes] = {0};
  return BuildAdminFrame(frame, kAdminVersionQuery, boardId, kNoBoard, sequence,
                         epoch, data);
}

inline size_t BuildVersionReportFrame(uint8_t* frame, uint8_t boardId,
                                      uint8_t sequence, uint8_t epoch,
                                      uint8_t firmwareMajor,
                                      uint8_t firmwareMinor,
                                      uint8_t firmwarePatch,
                                      uint8_t capabilities,
                                      uint8_t boardType) {
  uint8_t data[kAdminDataBytes] = {0};
  data[kAdminVersionFirmwareMajor] = firmwareMajor;
  data[kAdminVersionFirmwareMinor] = firmwareMinor;
  data[kAdminVersionFirmwarePatch] = firmwarePatch;
  data[kAdminVersionProtocolMajor] = kAdminProtocolMajor;
  data[kAdminVersionProtocolMinor] = kAdminProtocolMinor;
  data[kAdminVersionCapabilities] = capabilities;
  data[kAdminVersionBoardType] = boardType;
  return BuildAdminFrame(frame, kAdminVersionReport, boardId, kNoBoard,
                         sequence, epoch, data);
}

// --- firmware update ---------------------------------------------------------
//
// Update frames reuse the admin envelope's command/boardId prefix and then
// carry their own payload, so their sizes are computed rather than fixed. The
// chunk frame is the only large one on the bus.

constexpr size_t kAdminPrefixBytes = 2;  // command, boardId
constexpr size_t kUpdateBeginBodyBytes = 6;   // size (4) + crc (2)
constexpr size_t kUpdateAckBodyBytes = 5;     // status (1) + offset (4)
constexpr size_t kUpdateChunkHeadBytes = 6;   // offset (4) + length (2)

constexpr size_t kUpdateBeginFrameBytes =
    kHeaderBytes + kAdminPrefixBytes + kUpdateBeginBodyBytes + kCrcBytes;
constexpr size_t kUpdateAckFrameBytes =
    kHeaderBytes + kAdminPrefixBytes + kUpdateAckBodyBytes + kCrcBytes;
constexpr size_t kUpdateChunkMaxFrameBytes = kHeaderBytes + kAdminPrefixBytes +
                                             kUpdateChunkHeadBytes +
                                             kAdminChunkBytes + kCrcBytes;
constexpr size_t kUpdateCommitFrameBytes =
    kHeaderBytes + kAdminPrefixBytes + kCrcBytes;

inline size_t BuildUpdateBeginFrame(uint8_t* frame, uint8_t boardId,
                                    uint8_t sequence, uint8_t epoch,
                                    uint32_t imageBytes, uint16_t imageCrc) {
  WriteHeader(frame, kFrameAdmin, kFlagNone, kNoBoard, sequence, epoch);
  uint8_t* p = frame + kHeaderBytes;
  p[0] = kAdminUpdateBegin;
  p[1] = boardId;
  WriteU32(&p[2], imageBytes);
  WriteU16(&p[6], imageCrc);
  return AppendCrc(frame, kHeaderBytes + kAdminPrefixBytes +
                              kUpdateBeginBodyBytes);
}

inline void ReadUpdateBegin(const uint8_t* payload, uint32_t& imageBytes,
                            uint16_t& imageCrc) {
  imageBytes = ReadU32(&payload[kAdminPrefixBytes]);
  imageCrc = ReadU16(&payload[kAdminPrefixBytes + 4]);
}

// Used for both UpdateBeginAck and UpdateChunkAck: same shape, and the command
// byte says which. `offset` echoes the chunk being acknowledged, or the number
// of bytes staged so far for a begin.
inline size_t BuildUpdateAckFrame(uint8_t* frame, uint8_t command,
                                  uint8_t boardId, uint8_t sequence,
                                  uint8_t epoch, uint8_t status,
                                  uint32_t offset) {
  WriteHeader(frame, kFrameAdmin, kFlagNone, kNoBoard, sequence, epoch);
  uint8_t* p = frame + kHeaderBytes;
  p[0] = command;
  p[1] = boardId;
  p[2] = status;
  WriteU32(&p[3], offset);
  return AppendCrc(frame, kHeaderBytes + kAdminPrefixBytes +
                              kUpdateAckBodyBytes);
}

inline void ReadUpdateAck(const uint8_t* payload, uint8_t& status,
                          uint32_t& offset) {
  status = payload[kAdminPrefixBytes];
  offset = ReadU32(&payload[kAdminPrefixBytes + 1]);
}

inline size_t BuildUpdateChunkFrame(uint8_t* frame, uint8_t boardId,
                                    uint8_t sequence, uint8_t epoch,
                                    uint32_t offset, const uint8_t* data,
                                    uint16_t length) {
  WriteHeader(frame, kFrameAdmin, kFlagNone, kNoBoard, sequence, epoch);
  uint8_t* p = frame + kHeaderBytes;
  p[0] = kAdminUpdateChunk;
  p[1] = boardId;
  WriteU32(&p[2], offset);
  WriteU16(&p[6], length);
  for (uint16_t i = 0; i < length; ++i) {
    p[kAdminPrefixBytes + kUpdateChunkHeadBytes + i] = data[i];
  }
  return AppendCrc(frame, kHeaderBytes + kAdminPrefixBytes +
                              kUpdateChunkHeadBytes + length);
}

inline void ReadUpdateChunkHead(const uint8_t* payload, uint32_t& offset,
                                uint16_t& length) {
  offset = ReadU32(&payload[kAdminPrefixBytes]);
  length = ReadU16(&payload[kAdminPrefixBytes + 4]);
}

inline size_t UpdateChunkFrameBytes(uint16_t length) {
  return kHeaderBytes + kAdminPrefixBytes + kUpdateChunkHeadBytes + length +
         kCrcBytes;
}

// The largest frame that can appear on the bus, in either direction.
//
// Receive buffers must be at least this big. Sizing a buffer to the largest
// *runtime* frame is not enough: a firmware chunk is five times larger than
// anything the runtime protocol sends, and a short buffer would be a silent
// overrun rather than a rejected frame.
constexpr size_t kMaxRuntimeOutputFrameBytes =
    kHeaderBytes + kMaxCoilBytes + kMaxLampBytes + kGiBytes + kCrcBytes;
constexpr size_t kMaxRuntimeSwitchFrameBytes =
    kHeaderBytes + kSwitchStatusBytes + kMaxSwitchBytes + kCrcBytes;

constexpr size_t kMaxFrameBytes =
    kUpdateChunkMaxFrameBytes > kMaxRuntimeOutputFrameBytes
        ? (kUpdateChunkMaxFrameBytes > kMaxRuntimeSwitchFrameBytes
               ? kUpdateChunkMaxFrameBytes
               : kMaxRuntimeSwitchFrameBytes)
        : (kMaxRuntimeOutputFrameBytes > kMaxRuntimeSwitchFrameBytes
               ? kMaxRuntimeOutputFrameBytes
               : kMaxRuntimeSwitchFrameBytes);

inline size_t BuildUpdateCommitFrame(uint8_t* frame, uint8_t boardId,
                                     uint8_t sequence, uint8_t epoch) {
  WriteHeader(frame, kFrameAdmin, kFlagNone, kNoBoard, sequence, epoch);
  uint8_t* p = frame + kHeaderBytes;
  p[0] = kAdminUpdateCommit;
  p[1] = boardId;
  return AppendCrc(frame, kHeaderBytes + kAdminPrefixBytes);
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
