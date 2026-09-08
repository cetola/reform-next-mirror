/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH

  Machine specific Task
 */

#include "machine.h"
#include "hardware/irq.h"
#include "sysctl.h"

int64_t machine_task(__unused alarm_id_t id, void *user_data) {
  // Next specific housekeeping:
  // - process the two battery packs
  // - process the charger/dc-dc

  struct machine* mach = (struct machine*)user_data;

  battery_pack_task(&mach->packs[0], (float)MACHINE_TIMER_MS);
  battery_pack_task(&mach->packs[1], (float)MACHINE_TIMER_MS);
  charger_task(mach);
  charger_status(mach);

  return MACHINE_TIMER_MS * 1000;
}
