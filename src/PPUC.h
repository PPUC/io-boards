/**
  PPUC.h
  Created by Markus Kalkbrenner.
*/

#ifndef PPUC_h
#define PPUC_h

#define FIRMWARE_VERSION_MAJOR 0  // X Digits
#define FIRMWARE_VERSION_MINOR 2  // Max 2 Digits
#define FIRMWARE_VERSION_PATCH 0  // Max 2 Digits

#include <Arduino.h>

#include "PPUCPlatforms.h"
#include "PPUCTimings.h"

#define CONTROLLER_16_8_1 1

// Which board this image is built for, reported over the bus so the host can
// refuse to flash it to a different type. Must be one of ppuc::v2::BoardType.
//
// Overridable per PlatformIO environment: each board type gets its own
// environment and its own image, and the CI artefact name has to match
// ppuc::v2::BoardTypeName() for the host to pair them up on disk.
#ifndef PPUC_BOARD_TYPE
#define PPUC_BOARD_TYPE 1  // kBoardTypeIo16_8_1
#endif

// RS485 UART pins. These are not free to change: GPIO 0/1 are UART0 on the
// RP2040, and EventDispatcher releases the driver by polling uart0's BUSY flag
// directly rather than going through HardwareSerial::flush(). Moving the UART
// to other pins means moving that too - polling the wrong UART would report
// "idle" immediately and drop the driver mid-frame.
#define RS485_TX_PIN 0
#define RS485_RX_PIN 1
#define RS485_MODE_PIN 2

#include <EffectDevices/Definitions.h>

#endif
