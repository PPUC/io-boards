#include "ResetCause.h"

#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"

void NoteRebootCause(uint8_t cause) {
  watchdog_hw->scratch[kRebootMarkerScratch] = RebootMarker(cause);
}

void DetermineResetCause() {
  const uint32_t chipReset = vreg_and_chip_reset_hw->chip_reset;
  g_resetCause = ResetCauseFrom(
      watchdog_caused_reboot(), watchdog_hw->scratch[kRebootMarkerScratch],
      (chipReset & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_POR_BITS) != 0,
      (chipReset & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_RUN_BITS) != 0,
      (chipReset & VREG_AND_CHIP_RESET_CHIP_RESET_HAD_PSM_RESTART_BITS) != 0);
  // Read once. A later reboot that does not set it must not inherit it.
  watchdog_hw->scratch[kRebootMarkerScratch] = 0;
}
