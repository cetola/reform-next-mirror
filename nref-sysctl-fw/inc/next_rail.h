/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH
 */

#include <stdbool.h>
#include "machine_next.h"

bool syscon_warm_boot();
void turn_som_power_on(struct machine* mach);
void turn_som_power_off(struct machine* mach);
void som_wake();
void usb_host_5v_set(bool enable);
void next_rail_pd_aux_5v_enable();
void next_rail_pd_aux_5v_disable();
void enter_powersave();
