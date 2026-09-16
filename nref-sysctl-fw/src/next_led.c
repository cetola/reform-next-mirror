/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH
 */

#include "hardware/gpio.h"
#include "machine_next.h"
#include "next_gpio.h"
#include "next_led.h"

void led_indication_charging(bool on) {
  if (on) {
    gpio_put(PIN_LED_STATUS, 0);
    gpio_ext_pd_set_red_led(1);
  } else {
    gpio_put(PIN_LED_STATUS, 1);
    gpio_ext_pd_set_red_led(0);
  }
}
