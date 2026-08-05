#include "FirmwareUpdater.h"

#include <LittleFS.h>
#include <PicoOTA.h>

namespace {

// Where the incoming image is staged. The bootloader is told this name in the
// command file; nothing else reads it.
constexpr const char* kStagingPath = "/ppuc-firmware.bin";

// Read back in pieces to check the CRC. Sized to be comfortably smaller than
// the RP2040's stack, not to be fast - this runs once per update.
constexpr size_t kVerifyChunkBytes = 512;

// An image far larger than any firmware this project produces is a sign the
// host and board disagree about something, and it would fill the filesystem
// before failing. Current images are around 130 KB.
constexpr uint32_t kMaxImageBytes = 1024u * 1024u;

}  // namespace

bool FirmwareUpdater::ensureFilesystem() {
  if (m_filesystemReady) {
    return true;
  }
  // A board that has never staged an update has an unformatted filesystem, so
  // a first failure is expected rather than a fault.
  if (!LittleFS.begin()) {
    if (!LittleFS.format() || !LittleFS.begin()) {
      return false;
    }
  }
  m_filesystemReady = true;
  return true;
}

uint8_t FirmwareUpdater::begin(uint32_t imageBytes, uint16_t imageCrc) {
  if (m_state == State::kReceiving) {
    return ppuc::v2::kUpdateBusy;
  }
  if (imageBytes == 0 || imageBytes > kMaxImageBytes) {
    return ppuc::v2::kUpdateTooLarge;
  }
  if (!ensureFilesystem()) {
    return ppuc::v2::kUpdateUnsupported;
  }

  // Remove any earlier attempt first. Appending to a stale file would produce
  // an image that fails its CRC for a reason nobody could see.
  LittleFS.remove(kStagingPath);

  File file = LittleFS.open(kStagingPath, "w");
  if (!file) {
    return ppuc::v2::kUpdateWriteFailed;
  }
  file.close();

  m_expectedBytes = imageBytes;
  m_expectedCrc = imageCrc;
  m_received = 0;
  m_state = State::kReceiving;
  return ppuc::v2::kUpdateOk;
}

uint8_t FirmwareUpdater::chunk(uint32_t offset, const uint8_t* data,
                               uint16_t length) {
  if (m_state != State::kReceiving) {
    return ppuc::v2::kUpdateNotStaged;
  }
  // A repeat of the chunk just written is the host retrying after a lost ack.
  // Acknowledging it again is correct; writing it again is not.
  if (offset != m_received) {
    return ppuc::v2::kUpdateBadOffset;
  }
  if (length == 0 || offset + length > m_expectedBytes) {
    return ppuc::v2::kUpdateBadOffset;
  }

  File file = LittleFS.open(kStagingPath, "a");
  if (!file) {
    return ppuc::v2::kUpdateWriteFailed;
  }
  const size_t written = file.write(data, length);
  file.close();

  if (written != length) {
    return ppuc::v2::kUpdateWriteFailed;
  }

  m_received += length;
  return ppuc::v2::kUpdateOk;
}

uint8_t FirmwareUpdater::commit() {
  if (m_state != State::kReceiving || m_received != m_expectedBytes) {
    return ppuc::v2::kUpdateNotStaged;
  }

  // Verify what actually landed in flash, not what we believe we wrote. This
  // is the last point at which a bad image can be refused, and refusing costs
  // a retransmission where accepting costs a board that needs a USB cable.
  File file = LittleFS.open(kStagingPath, "r");
  if (!file) {
    return ppuc::v2::kUpdateWriteFailed;
  }

  uint16_t crc = 0xFFFF;
  uint8_t buffer[kVerifyChunkBytes];
  uint32_t verified = 0;
  while (verified < m_expectedBytes) {
    const size_t want = (m_expectedBytes - verified) < kVerifyChunkBytes
                            ? (m_expectedBytes - verified)
                            : kVerifyChunkBytes;
    const int read = file.read(buffer, want);
    if (read <= 0) {
      break;
    }
    // Continue the CRC across chunks rather than restarting it per buffer.
    for (int i = 0; i < read; ++i) {
      crc ^= static_cast<uint16_t>(buffer[i]) << 8;
      for (uint8_t bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                             : static_cast<uint16_t>(crc << 1);
      }
    }
    verified += static_cast<uint32_t>(read);
  }
  file.close();

  if (verified != m_expectedBytes || crc != m_expectedCrc) {
    abort();
    return ppuc::v2::kUpdateCrcMismatch;
  }

  // Hand over to the bootloader. It copies on the next boot and clears the
  // command itself, so an interrupted copy simply repeats rather than leaving
  // the board half-written.
  picoOTA.begin();
  if (!picoOTA.addFile(kStagingPath)) {
    abort();
    return ppuc::v2::kUpdateWriteFailed;
  }
  if (!picoOTA.commit()) {
    abort();
    return ppuc::v2::kUpdateWriteFailed;
  }

  m_state = State::kStaged;
  return ppuc::v2::kUpdateOk;
}

void FirmwareUpdater::abort() {
  if (m_filesystemReady) {
    LittleFS.remove(kStagingPath);
  }
  m_state = State::kIdle;
  m_expectedBytes = 0;
  m_expectedCrc = 0;
  m_received = 0;
}
