/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH

  MNT Reform Next: SoC UART forwarding through SC USB UART
 */

#ifndef _FORWARD_UART_H
#define _FORWARD_UART_H

#include <stdbool.h>

void set_forward_uart_mode(bool on);
bool get_forward_uart_mode();
void forward_soc_uart();

#endif
