/*
 SPDX-License-Identifier: BSD-3-Clause
 Copyright (c) 2024 Chris Hofstaedtler
*/
#pragma once

#define TUD_BOS_DS20_UUID   \
    0x63, 0xec, 0x0a, 0x01, 0x74, 0xf5, 0xcd, 0x52, \
    0x9d, 0xda, 0x28, 0x52, 0x55, 0x0d, 0x94, 0xf0

#define TUD_BOS_DS_20_DESC_LEN   28

#define TUD_BOS_DS20_DESCRIPTOR(_desc_set_len, _vendor_code) \
    TUD_BOS_PLATFORM_DESCRIPTOR(TUD_BOS_DS20_UUID, U32_TO_U8S_LE(0x0001090e), U16_TO_U8S_LE(_desc_set_len), _vendor_code, 0)

/* Minimum bsdUSB to export a BOS descriptor */
#define BCD_USB_MIN_FOR_BOS  0x0210
