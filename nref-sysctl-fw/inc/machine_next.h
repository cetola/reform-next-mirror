#ifndef _MACHINE_NEXT_H
#define _MACHINE_NEXT_H

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

#define PIN_LED_STATUS 20
#define PIN_UNUSED_1 21
#define PIN_HSTX_HPD 22

#define PIN_CHRG_CFG 23
#define PIN_CHRG_ALERT 24

#define PIN_BACKLIGHT_EN 25
#define PIN_BACKLIGHT_PWM 26

#define PIN_SOM_WAKE 27
#define PIN_SOM_UART_TX 28
#define PIN_SOM_UART_RX 29

// Keyboard Control UART
#define UART_ID uart1

// FIXME: the following are now on PCA9536DP (on SDA/SCL1)
// 3V3_ENABLE
// 5V_ENABLE
// HDMI_DP_SWITCH
// ~QON

// FUSB302B USB-PD controller
#define FUSB_ADDR 0x22

#include "next_pack.h"

struct machine {
  bool som_is_powered;
  struct battery_pack packs[2];

  float battery_volts;
  float battery_amps;
  float input_volts;
  int charge_percentage;

  // settings
  int charger_charge_current_ma;
  int charger_input_current_ma;
  int cell_max_mah;

  // metadata
  bool print_pack_info;
  uint16_t ticks;
};

#include "next_init.h"
#include "next_led.h"
#include "next_rail.h"
#include "next_charger.h"
#include "next_command.h"
#include "hardware/irq.h"

int64_t machine_task(alarm_id_t id, void *user_data);

#endif
