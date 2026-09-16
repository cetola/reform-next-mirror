/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2023-2026 MNT Research GmbH

  fusb_read/write functions based on:
  https://git.clarahobbs.com/pd-buddy/pd-buddy-firmware/src/branch/master/lib/src/fusb302b.c
*/
#include <stdio.h>

#include "hardware/irq.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"
#include "hardware/structs/watchdog.h"

#include "pico/divider.h"
#include "tusb.h"
#include "reform_stdio_usb.h"
#include "cli.h"
#include "cli_usb.h"
#include "forward_uart.h"
#include "machine.h"
#include "pd_com.h"
#include "spi_com.h"
#include "sysctl.h"
#include "uart_com.h"

static alarm_pool_t* task_alarm_pool;
static int ALARM_IRQ = 0;
static struct machine mach = {0};

void setup() {
  // used for stdio and reset interface
  tusb_init();
  reform_stdio_usb_init();

  // reset if main loop is stuck for 10 seconds
  watchdog_enable(WATCHDOG_MS, 1);

  // init CLI
  // init platform specific IOs
  cli_init_env();
  // machine_init calls hwapi_init() and charger_init() at the end
  machine_init(&mach);
  uart_com_init(&mach);
  cli_usb_init(&mach);

  // init SPI client for SoC OS driver (mnt-sc)
  init_spi_client(&mach);

  // if this is a warm boot, then keep power rail state
  if (syscon_warm_boot()) {
    // on by default after reboot
    printf("# [reset] warm boot, restoring power.\n");
    turn_som_power_on(&mach);
  } else {
    // off by default
    turn_som_power_off(&mach);
  }

  pd_init();
}

bool spi_commands_task_old(__unused struct repeating_timer *t) {
  // handle commands from SoM via SPI
  handle_spi_commands(&mach);
  // timer should continue calling us
  return true;
}

void sysctl_disable_irqs() {
  irq_set_enabled(IO_IRQ_BANK0, false);
}

void sysctl_enable_irqs() {
  irq_set_enabled(IO_IRQ_BANK0, true);
}

// from pico-sdk docs:
// https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#function-documentation-10
// IRQ handlers set up with gpio_set_irq... are acknowledged automatically.
void spi_commands_task([[maybe_unused]] unsigned int gpio, [[maybe_unused]] long unsigned int event) {
  // handle commands from SoM
  // TODO: pass cli state/handle
  if (gpio != PIN_SOM_SS0) return;
  sysctl_disable_irqs();
  handle_spi_commands(&mach);
  sysctl_enable_irqs();
}

void loop() {
  bool can_sleep = true;

  // feed watchdog reset
  watchdog_update();

  // handle commands from keyboard
  handle_uart_commands();

  // handle commands over usb serial
  if (get_forward_uart_mode()) {
    forward_soc_uart();
  } else {
    handle_usb_commands();
  }

  irq_set_enabled(ALARM_IRQ, false);
  if (!pd_tick(&mach)) {
    can_sleep = false;
  }
  irq_set_enabled(ALARM_IRQ, true);
  // TODO FIXME use timer instead
  mach.ticks++;

  if (can_sleep) {
    sleep_us(100); // one tick is 0.1ms
  }
}

void mntre_reset_callback(void) {
  // TODO
}

int main() {
  setup();

  sleep_ms(500);
  printf("# [next_sysctl] sleep before main loop\n");
  
  ALARM_IRQ = timer_hardware_alarm_get_irq_num(timer_hw, 2);

  // call SPI task every 5ms to ensure response time
  // struct repeating_timer spi_timer;
  // add_repeating_timer_ms(-5, spi_commands_task, NULL, &spi_timer);
  gpio_set_irq_enabled_with_callback(PIN_SOM_SS0, GPIO_IRQ_EDGE_FALL|GPIO_IRQ_EDGE_RISE, true, &spi_commands_task);

  // call configure task every few seconds
  // but at a lower priority than i.e. USB
  // via https://github.com/raspberrypi/pico-sdk/issues/751#issuecomment-1062078338
  task_alarm_pool = alarm_pool_create(2, 16); // create an alarm pool, use hardware alarm #2
  irq_set_priority(ALARM_IRQ, 0xc0); // larger number is lower priority
  alarm_pool_add_alarm_in_ms(task_alarm_pool, MACHINE_TIMER_MS, machine_task, &mach, false);

  printf("# [next_sysctl] entering main loop\n");

  while (true) {
    loop();
  }

  return 0;
}
