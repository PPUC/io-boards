#include "FirmwareUpdater.h"

#include <LittleFS.h>

namespace {
// See the note in the header: one board, one updater, one staging file.
File g_stagingFile;
}
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
  // A staging state left over from an earlier attempt is not a reason to
  // refuse. There is one host and one bus, so a fresh UpdateBegin means that
  // host has restarted the transfer - it does not mean two updates are racing.
  // Refusing here meant a single interrupted update wedged the board into
  // kUpdateBusy for every later attempt until someone power cycled it, which is
  // exactly the situation where retrying has to work.
  if (m_state == State::kReceiving) {
    m_state = State::kIdle;
    m_received = 0;
  }
  if (imageBytes == 0 || imageBytes > kMaxImageBytes) {
    return ppuc::v2::kUpdateTooLarge;
  }
  if (!ensureFilesystem()) {
    return ppuc::v2::kUpdateUnsupported;
  }

  // Remove any earlier attempt first. Appending to a stale file would produce
  // an image that fails its CRC for a reason nobody could see.
  if (g_stagingFile) {
    g_stagingFile.close();
  }
  m_buffered = 0;
  LittleFS.remove(kStagingPath);

  g_stagingFile = LittleFS.open(kStagingPath, "w");
  if (!g_stagingFile) {
    return ppuc::v2::kUpdateWriteFailed;
  }

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
  // A repeat of a chunk already written is the host retrying after a lost ack.
  // Acknowledging it again is correct; writing it again is not.
  //
  // This is what the comment always said, but the check underneath it rejected
  // any offset that was not the next one - including a repeat - so a single
  // dropped ack ended the update with kUpdateBadOffset rather than being
  // absorbed by the retry that exists for exactly this case.
  if (offset < m_received) {
    return ppuc::v2::kUpdateOk;
  }
  if (offset > m_received) {
    // A gap: the host skipped ahead, so the staged image would be incomplete.
    return ppuc::v2::kUpdateBadOffset;
  }
  if (length == 0 || offset + length > m_expectedBytes) {
    return ppuc::v2::kUpdateBadOffset;
  }

  if (!g_stagingFile) {
    return ppuc::v2::kUpdateWriteFailed;
  }

  if (m_buffered + length > kWriteBufferBytes && !flushBuffer()) {
    return ppuc::v2::kUpdateWriteFailed;
  }
  // A chunk never exceeds kAdminChunkBytes, so it always fits once flushed.
  memcpy(m_buffer + m_buffered, data, length);
  m_buffered += length;

  m_received += length;
  if (m_received == m_expectedBytes && !flushBuffer()) {
    return ppuc::v2::kUpdateWriteFailed;
  }
  return ppuc::v2::kUpdateOk;
}

bool FirmwareUpdater::flushBuffer() {
  if (m_buffered == 0) {
    return true;
  }
  if (!g_stagingFile) {
    return false;
  }
  const size_t written = g_stagingFile.write(m_buffer, m_buffered);
  if (written != m_buffered) {
    return false;
  }
  m_buffered = 0;
  // Flushed to the filesystem, not merely to our own buffer: commit() reopens
  // the file to verify it, and would otherwise read a short image.
  g_stagingFile.flush();
  return true;
}

uint8_t FirmwareUpdater::commit() {
  if (m_state != State::kReceiving || m_received != m_expectedBytes) {
    return ppuc::v2::kUpdateNotStaged;
  }

  // Everything buffered must be on the filesystem before it is read back, and
  // the write handle closed, or the verification below reads a short image.
  if (!flushBuffer()) {
    return ppuc::v2::kUpdateWriteFailed;
  }
  if (g_stagingFile) {
    g_stagingFile.close();
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
  // Drop buffered bytes and the write handle before removing the file, so an
  // abandoned transfer leaves nothing half-written behind it. A partial staging
  // file is not harmless: mounting one has hung a board hard enough to need the
  // power switch.
  m_buffered = 0;
  if (g_stagingFile) {
    g_stagingFile.close();
  }
  if (m_filesystemReady) {
    LittleFS.remove(kStagingPath);
  }
  m_state = State::kIdle;
  m_expectedBytes = 0;
  m_expectedCrc = 0;
  m_received = 0;
}
