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
#include "next_pack.h"

// #define OTG_AS_5V // WARNING: defining this requires the hardware mod described in https://source.mnt.re/reform/pocket-reform/-/issues/3
// #define FACTORY_MODE // turn device on immediately after starting sysctl
#define ACM_ENABLED 1 // usb serial control for debugging

#define FW_STRING1 "NREF1SYS"
#define FW_STRING2 "R1"

#define PIN_SDA0 0
#define PIN_SCL0 1
#define PIN_SDA1 2
#define PIN_SCL1 3

#define PIN_KBD_UART_TX 4
#define PIN_KBD_UART_RX 5

#define PIN_BAT1_ALERT 6
#define PIN_BAT2_ALERT 7

#define PIN_SOM_MOSI 8
#define PIN_SOM_SS0 9
#define PIN_SOM_SCK 10
#define PIN_SOM_MISO 11

#define PIN_HSTX_D0P 12
#define PIN_HSTX_D0N 13
#define PIN_HSTX_DCKP 14
#define PIN_HSTX_DCKN 15
#define PIN_HSTX_D2P 16
#define PIN_HSTX_D2N 17
#define PIN_HSTX_D1P 18
#define PIN_HSTX_D1N 19

#define PIN_LED_B 20
#define PIN_LED_R 21
#define PIN_LED_G 22

#define PIN_CHRG_CFG 23
#define PIN_CHRG_ALERT 24

#define PIN_BACKLIGHT_EN 25
#define PIN_BACKLIGHT_PWM 26

#define PIN_SOM_WAKE 27
#define PIN_SOM_UART_TX 28
#define PIN_SOM_UART_RX 29

// FIXME: the following are now on PCA9536DP (on SDA/SCL1)
// 3V3_ENABLE
// 5V_ENABLE
// HDMI_DP_SWITCH
// ~QON

// FUSB302B USB-PD controller
#define FUSB_ADDR 0x22

#define I2C_TIMEOUT (1000 * 500)

#define UART_ID uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define BOOT_MAGIC_2 0xAA55F0F0
#define BOOT_MAGIC_3 0x0F0F55AA
#define BOOT_MAGIC_OFF (io_rw_32)(-1)

//#define BATTERY_CAPACITY_MILLIAMP_HOURS 4000

#include "pd_com.h"

typedef struct battery_info_s
{
    bool som_is_powered;
  
    struct BatteryPack packs[2];

    // reported by charger
    float battery_volts;
    float battery_amps;
    float input_volts;

    // reported by balancer
    float cell1_volts;
    float cell2_volts;
    int charge_percentage;

    // metadata
    bool print_pack_info;
    uint16_t ticks;
} battery_info_s;

#include "fusb302b.h"
#include "pd.h"
#include "uart_com.h"
#include "spi_com.h"

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
