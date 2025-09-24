/*
 SPDX-License-Identifier: BSD-3-Clause
 Copyright (c) 2024 Chris Hofstaedtler

 MNT Research Reset Interface private (firmware side) header
*/
#pragma once

// VENDOR sub-class for the reset interface
#define MNTRE_RESET_INTERFACE_SUBCLASS 0x00
// VENDOR protocol for the reset interface
#define MNTRE_RESET_INTERFACE_PROTOCOL 0x01

// CONTROL requests:
// reset to BOOTSEL
#define MNTRE_RESET_REQUEST_BOOTSEL 0x01
// reset into application
#define MNTRE_RESET_REQUEST_RESET 0x02

// String name of the interface
#define MNTRE_RESET_INTERFACE_NAME_STR "Reset"

// TinyUSB descriptor length
#define MNTRE_RESET_TUD_DESC_LEN 9

// TinyUSB descriptor
#define MNTRE_RESET_TUD_DESCRIPTOR(_itfnum, _stridx) \
  /* Interface */\
  MNTRE_RESET_TUD_DESC_LEN, TUSB_DESC_INTERFACE, _itfnum, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC, MNTRE_RESET_INTERFACE_SUBCLASS, MNTRE_RESET_INTERFACE_PROTOCOL, _stridx,
