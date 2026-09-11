/*
  FirmwareUpdater.h.
  Created by Markus Kalkbrenner, 2026.

  Receiving a firmware image over RS485 and handing it to the OTA bootloader.

  Deliberately writes no flash itself. The Arduino-Pico core already carries a
  power-fail-safe "stage 3" OTA bootloader - it is linked into every build and
  the 12 KB it occupies is already spent - which mounts LittleFS on boot,
  looks for a command file, and copies the named image into application flash.
  If power is lost during that copy it simply starts again on the next boot,
  because both the image and the command are in flash. Writing our own flash
  routine would mean reproducing that, worse.

  So the job here is only: collect the bytes, prove they are the bytes that
  were sent, and point the bootloader at them.

  Play more pinball!
*/

#ifndef FirmwareUpdater_h
#define FirmwareUpdater_h

#include <Arduino.h>

#include "PPUCProtocolV2.h"

class FirmwareUpdater {
 public:
  // Announces an incoming image. Erases any previous staging file and opens a
  // new one. Returns an AdminUpdateStatus.
  uint8_t begin(uint32_t imageBytes, uint16_t imageCrc);

  // Appends one chunk. `offset` must be exactly where the previous chunk
  // ended: a gap would leave a hole in the image that the CRC would catch
  // later, but rejecting it here says which chunk went missing.
  uint8_t chunk(uint32_t offset, const uint8_t* data, uint16_t length);

  // Verifies the staged image against the announced CRC and, if it matches,
  // writes the bootloader's command file. Does not return on success: the
  // board reboots into the bootloader, which performs the copy.
  uint8_t commit();

  // Abandons a transfer and removes the staging file, so a failed attempt
  // cannot be mistaken for a complete one later.
  void abort();

  bool inProgress() const { return m_state == State::kReceiving; }
  uint32_t bytesReceived() const { return m_received; }

 private:
  enum class State : uint8_t { kIdle, kReceiving, kStaged };

  bool ensureFilesystem();
  // Writes whatever is buffered to the staging file. The file is held open for
  // the whole transfer, so this is the only place bytes reach the filesystem.
  bool flushBuffer();

  State m_state = State::kIdle;
  uint32_t m_expectedBytes = 0;
  uint16_t m_expectedCrc = 0;
  uint32_t m_received = 0;
  bool m_filesystemReady = false;

  // Chunks are accumulated and written a page at a time rather than one
  // open/append/close per chunk, which is where a transfer spent almost all of
  // its time: 266 ms per 256-byte chunk against 23 ms of wire time, so roughly
  // three minutes per board for a filesystem cost, not a bus cost.
  //
  // This changes nothing about what can be installed. commit() reads the staged
  // file back out of flash and checks it against the announced CRC before the
  // bootloader is pointed at anything, so a short or corrupt write is caught at
  // the same gate it always was.
  static constexpr size_t kWriteBufferBytes = 4096;
  uint8_t m_buffer[kWriteBufferBytes] = {0};
  size_t m_buffered = 0;
  // The staging file handle lives in the .cpp rather than here: this header is
  // reached by the native test build through EventDispatcher.h, where LittleFS
  // does not exist. One board has one updater, so a single handle beside the
  // implementation is equivalent and costs the tests nothing.
};

#endif
