#ifndef _NEXT_SYSCTL_H
#define _NEXT_SYSCTL_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/rtc.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/vreg_and_chip_reset.h"

// #define OTG_AS_5V // WARNING: defining this requires the hardware mod described in https://source.mnt.re/reform/pocket-reform/-/issues/3
// #define FACTORY_MODE // turn device on immediately after starting sysctl
// #define ACM_ENABLED // usb serial control for debugging
// #define PREF_DISPLAY_V2 // backlight control for second type of display, TOP070F01A (not LT070ME05000)

#define FW_STRING1 "NREF1SYS"
#define FW_STRING2 "R1"

#define PIN_SDA 0
#define PIN_SCL 1

#define PIN_DISP_RESET 2
#define PIN_FLIGHTMODE 3
#define PIN_KBD_UART_TX 4
#define PIN_KBD_UART_RX 5
#define PIN_WOWWAN 6
#define PIN_DISP_EN 7
#define PIN_SOM_MOSI 8
#define PIN_SOM_SS0 9
#define PIN_SOM_SCK 10
#define PIN_SOM_MISO 11
#define PIN_SOM_UART_TX 12
#define PIN_SOM_UART_RX 13
#define PIN_FUSB_INT 14
#define PIN_LED_B 15
#define PIN_LED_R 16
#define PIN_LED_G 17
#define PIN_MODEM_POWER 18
#define PIN_SOM_WAKE 19
#define PIN_MODEM_RESET 20
#define PIN_1V1_ENABLE 23
#define PIN_3V3_ENABLE 24
#define PIN_5V_ENABLE 25
#define PIN_PHONE_DPR 27
#define PIN_USB_SRC_ENABLE 28
#define PIN_PWREN_LATCH 29

// FUSB302B USB-PD controller
#define FUSB_ADDR 0x22
// MAX17320 protector/balancer
// https://datasheets.maximintegrated.com/en/ds/MAX17320.pdf
#define MAX_ADDR1 0x36
#define MAX_ADDR2 0x0b
// MP2650 charger
// https://www.monolithicpower.com/en/documentview/productdocument/index/version/2/document_type/Datasheet/lang/en/sku/MP2650GV/document_id/9664/
#define MPS_ADDR 0x5c

#define I2C_TIMEOUT (1000 * 500)

#define UART_ID uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define BOOT_MAGIC_2 0xAA55F0F0
#define BOOT_MAGIC_3 0x0F0F55AA
#define BOOT_MAGIC_OFF (io_rw_32)(-1)

#define BATTERY_CAPACITY_MILLIAMP_HOURS 4000

#include "pd_com.h"

typedef struct battery_info_s
{
    bool som_is_powered;

    // reported by charger
    float battery_volts;
    float battery_amps;
    float input_volts;

    // reported by balancer
    float cell1_volts;
    float cell2_volts;
    int charge_percentage;
    float time_to_empty;

    // metadata
    bool print_pack_info;
    uint16_t max17320_devname;
    uint16_t ticks;
} battery_info_s;

#include "fusb302b.h"
#include "pd.h"
#include "uart_com.h"
#include "spi_com.h"
#include "max17320.h"
#include "mp2650.h"

// Shared functions with communication classes
void som_wake();
void turn_som_power_on();
void turn_som_power_off();
void set_display_backlight(int percent);

void usb_host_5v_enable();
void usb_host_5v_disable();
void charger_enable_charge(int current);
void charger_disable_charge();

#endif
