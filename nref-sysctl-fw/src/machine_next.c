/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH

  Machine specific Init & Task
 */

#include "machine_next.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "next_gpio.h"
#include "next_led.h"
#include "next_mux.h"
#include "next_charger.h"
#include "next_command.h"
#include "sysctl.h"

void machine_init(struct machine* mach) {
  // UART to keyboard
  uart_init(UART_ID, BAUD_RATE);
  uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
  uart_set_hw_flow(UART_ID, false, false);
  uart_set_fifo_enabled(UART_ID, true);
  gpio_set_function(PIN_KBD_UART_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_KBD_UART_RX, GPIO_FUNC_UART);

  // UART to som
  uart_init(uart0, BAUD_RATE);
  uart_set_format(uart0, DATA_BITS, STOP_BITS, PARITY);
  uart_set_hw_flow(uart0, false, false);
  uart_set_fifo_enabled(uart0, true);
  //gpio_set_function(PIN_SOM_UART_TX, GPIO_FUNC_UART);
  // only listen by default, don't disturb usb-uart TX
  gpio_init(PIN_SOM_UART_TX);
  gpio_set_dir(PIN_SOM_UART_TX, 0);
  gpio_set_function(PIN_SOM_UART_RX, GPIO_FUNC_UART);

  // I2C0
  gpio_set_function(PIN_SDA0, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL0, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA0, PIN_SCL0, GPIO_FUNC_I2C));
  i2c_init(i2c0, 100 * 1000);
  mach->packs[0].id = 0;
  mach->packs[0].i2c = i2c0;

  // I2C1
  gpio_set_function(PIN_SDA1, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL1, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA1, PIN_SCL1, GPIO_FUNC_I2C));
  i2c_init(i2c1, 100 * 1000);
  mach->packs[1].id = 1;
  mach->packs[1].i2c = i2c1;

  // motherboard external GPIOs
  gpio_mb_setup(syscon_warm_boot());
  // left port board (PD) GPIOs
  gpio_ext_pd_setup();

  // LED
  gpio_init(PIN_LED_STATUS);
  gpio_set_dir(PIN_LED_STATUS, 1);
  led_indication_charging(false);

  // SoM / SoC wake GPIO
  gpio_init(PIN_SOM_WAKE);
  gpio_set_dir(PIN_SOM_WAKE, GPIO_OUT);
  gpio_put(PIN_SOM_WAKE, 0);

  // backlight control
  // TODO: experimental
  gpio_init(PIN_BACKLIGHT_EN);
  gpio_set_dir(PIN_BACKLIGHT_EN, GPIO_IN);
  gpio_init(PIN_BACKLIGHT_PWM);
  gpio_set_dir(PIN_BACKLIGHT_PWM, GPIO_IN);

  // by default, allow sysctl flashing from the outside
  mux_set_usb_mode(0, 0);

  // CLI init
  hwapi_init();
  charger_init(mach);
}

void power_calculations(struct machine *mach) {
  mach->charge_percentage = 0;
  mach->battery_amps = 0;
  mach->battery_volts = 0;
  float num = 0;
  for (int i = 0; i < 2; i++) {
    if (mach->packs[i].active) {
      mach->charge_percentage += mach->packs[i].gauge_percent;
      mach->battery_volts += mach->packs[i].volt;
      mach->battery_amps += mach->packs[i].ampere;
      num++;
    }
  }
  // average paralleled pack voltages, gauge
  if (num >= 1.0) {
    mach->charge_percentage /= num;
    mach->battery_volts /= num;
  }
}

int64_t machine_task(__unused alarm_id_t id, void *user_data) {
  // Next specific housekeeping:
  // - process the two battery packs
  // - process the charger/dc-dc

  struct machine* mach = (struct machine*)user_data;

  battery_pack_task(mach, &mach->packs[0], (float)MACHINE_TIMER_MS);
  battery_pack_task(mach, &mach->packs[1], (float)MACHINE_TIMER_MS);
  charger_task(mach);
  charger_status(mach);
  power_calculations(mach);

  return MACHINE_TIMER_MS * 1000;
}
