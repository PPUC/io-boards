/**
  PPUC.h
  Created by Markus Kalkbrenner.
*/

#ifndef PPUC_h
#define PPUC_h

#include <Arduino.h>

#include "PPUCFirmwareVersion.h"
#include "PPUCPlatforms.h"
#include "PPUCTimings.h"

#define FIRMWARE_VERSION_MAJOR PPUC_FIRMWARE_VERSION_MAJOR
#define FIRMWARE_VERSION_MINOR PPUC_FIRMWARE_VERSION_MINOR
#define FIRMWARE_VERSION_PATCH PPUC_FIRMWARE_VERSION_PATCH

#define CONTROLLER_16_8_1 1

#define RS485_MODE_PIN 2

#include <EffectDevices/Definitions.h>

#endif
