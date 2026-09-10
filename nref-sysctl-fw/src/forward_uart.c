/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH

  MNT Reform Next: SoC UART forwarding through SC USB UART
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "forward_uart.h"
#include "machine.h"

// FIXME put in machine struct
static bool _fwd_enabled = false;

void set_forward_uart_mode(bool on) {
  _fwd_enabled = on;
  if (on) {
    gpio_set_function(PIN_SOM_UART_TX, GPIO_FUNC_UART);
    printf("\n# --- entered console forward mode ---\n");
  } else {
    gpio_init(PIN_SOM_UART_TX);
    gpio_set_dir(PIN_SOM_UART_TX, 0);
    printf("\n# --- exited console forward mode ---\n");
  }
}

bool get_forward_uart_mode() {
  return _fwd_enabled;
}

void forward_soc_uart() {
  if (!_fwd_enabled) return;

  // prevent endless loop
  int uart_max = 64;
  while (uart_is_readable(uart0) && uart_max > 0) {
    putc(uart_getc(uart0), stdout);
    uart_max--;
  }
  int usb_c = getchar_timeout_us(0);
  if (usb_c != PICO_ERROR_TIMEOUT) {
    if (usb_c == 16) {
      // "data link escape", ctrl+p
      set_forward_uart_mode(false);
    } else {
      uart_putc(uart0, usb_c);
    }
  }
}
