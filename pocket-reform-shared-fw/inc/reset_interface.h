/*
 SPDX-License-Identifier: BSD-3-Clause
 Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 Copyright (c) 2024 Chris Hofstaedtler
*/
#pragma once

// Enable/disable resetting into BOOTSEL mode if the host sets the baud rate to a magic value (PICO_STDIO_USB_RESET_MAGIC_BAUD_RATE), type=bool, default=1, group=pico_stdio_usb
#ifndef RESET_INTERFACE_ENABLE_RESET_VIA_BAUD_RATE
#define RESET_INTERFACE_ENABLE_RESET_VIA_BAUD_RATE 1
#endif

// baud rate that if selected causes a reset into BOOTSEL mode (if PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE is set), default=1200, group=pico_stdio_usb
#define RESET_INTERFACE_RESET_MAGIC_BAUD_RATE 1200

// delays in ms before rebooting via regular flash boot, default=100, group=pico_stdio_usb
#define RESET_INTERFACE_RESET_TO_FLASH_DELAY_MS 100

// Will be called just before resetting. Each firmware implementation must define it.
void mntre_reset_callback(void);
