#ifndef _MNT_SYSCTL_H
#define _MNT_SYSCTL_H

#include <ctype.h>

#define MACHINE_TIMER_MS 2000
#define WATCHDOG_MS 10000

#define I2C_TIMEOUT (1000 * 500)

#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define BOOT_MAGIC_2 0xAA55F0F0
#define BOOT_MAGIC_3 0x0F0F55AA
#define BOOT_MAGIC_OFF (io_rw_32)(-1)

#endif
