/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH
 */

#ifndef POCKET_PD_COM_H
#define POCKET_PD_COM_H

#include <stdbool.h>
#include "machine.h"

#define PD_STATE_SETUP 0
#define PD_STATE_UNATTACHED 1
#define PD_STATE_UNATTACHED_SNK 2
#define PD_STATE_ATTACHED_SNK 3
#define PD_STATE_UNATTACHED_SRC 4
#define PD_STATE_ATTACHED_SRC 5

void pd_init();
bool pd_tick(struct machine* mach);

// TODO: ???
unsigned int pd_get_state_for_debug();

#endif
