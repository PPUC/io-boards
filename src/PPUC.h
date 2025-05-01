/**
  PPUC.h
  Created by Markus Kalkbrenner.
*/

#ifndef PPUC_h
#define PPUC_h

#define FIRMWARE_VERSION_MAJOR 0  // X Digits
#define FIRMWARE_VERSION_MINOR 1  // Max 2 Digits
#define FIRMWARE_VERSION_PATCH 0  // Max 2 Digits

#include <Arduino.h>

#include "PPUCPlatforms.h"

#define CONTROLLER_MEGA_ALL_INPUT 1
#define CONTROLLER_TEENSY_OUTPUT 10
#define CONTROLLER_TEENSY_OUTPUT_2 11
#define CONTROLLER_PICO_OUTPUT 20
#define CONTROLLER_NANO_PIN2DMD_OUTPUT 30
#define CONTROLLER_16_8_1 40

#include <EffectDevices/Definitions.h>

#endif
