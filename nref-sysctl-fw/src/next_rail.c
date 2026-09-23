/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH
 */

#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/structs/watchdog.h"
#include "hardware/uart.h"
#include "next_rail.h"
#include "next_gpio.h"
#include "next_mux.h"
#include "spi_com.h"
#include "machine_next.h"

bool syscon_warm_boot() {
  return (watchdog_hw->reason == 1);
}

void turn_som_power_on(struct machine *mach) {
  printf("# [action] turn_som_power_on\n");
  init_spi_client(mach);

  gpio_ext_pd_poweron_defaults();
  gpio_mb_enable(GPIO_EXT_3V3_EN);
  gpio_mb_enable(GPIO_EXT_5V_EN);
  gpio_put(PIN_BACKLIGHT_EN, 1);
  
  mach->som_is_powered = true;

  // present usb-uart on charging port by default,
  // and activate internal usb hub
  mux_set_usb_mode(0, 1);
}

/*
  this function can be called from a timer interrupt
  in the spi command handler, no sleep is allowed here.
  if delays should become necessary, they have to be
  busy loops.
*/
void turn_som_power_off(struct machine* mach) {
  printf("# [action] turn_som_power_off\n");

  gpio_mb_disable(GPIO_EXT_5V_EN);
  gpio_mb_disable(GPIO_EXT_3V3_EN);
  gpio_put(PIN_BACKLIGHT_EN, 0);
  gpio_ext_pd_poweroff_defaults();

  mach->som_is_powered = false;

  // present sysctl usb on charging port
  mux_set_usb_mode(0, 0);
}

void som_wake() {
  gpio_put(PIN_SOM_WAKE, 1);
  sleep_ms(5);
  gpio_put(PIN_SOM_WAKE, 0);
  uart_puts(uart0, "wake\r\n");
}

void usb_host_5v_set(bool enable) {
  gpio_ext_pd_usb_5v_src_set(enable);
}

void next_rail_pd_aux_5v_enable() {
  gpio_ext_pd_enable(7);
}

void next_rail_pd_aux_5v_disable() {
  gpio_ext_pd_disable(7);
}

// TODO implement deep sleep
void enter_powersave() {
  // nop
}

