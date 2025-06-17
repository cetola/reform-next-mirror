/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2023-2025 MNT Research GmbH

  fusb_read/write functions based on:
  https://git.clarahobbs.com/pd-buddy/pd-buddy-firmware/src/branch/master/lib/src/fusb302b.c
*/
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

#include "sysctl.h"
#include "pico/divider.h"
#include "tusb.h"
#include "reform_stdio_usb.h"
#include "next_gpio.h"
#include "bq25792.h"
#include "bq76922.h"


// FIXME
battery_info_s battery_info = {0};
static int ALARM_IRQ = 0;

// The Pico boot rom uses watchdog scratch registers 0, 1, 4, 5, 6, and 7.
// That leaves 2 and 3 for our "system is on" magic.
// A _real_ power-on reset clears these registers, so if our magic is left over
// then we have either been updated while the system is on, or have run into an
// event with probability 2**-64.
bool syscon_warm_boot()
{
  return (watchdog_hw->scratch[2] == BOOT_MAGIC_2 &&
          watchdog_hw->scratch[3] == BOOT_MAGIC_3);
}

void set_boot_magic()
{
  watchdog_hw->scratch[2] = BOOT_MAGIC_2;
  watchdog_hw->scratch[3] = BOOT_MAGIC_3;
}

void clear_boot_magic()
{
  watchdog_hw->scratch[2] = BOOT_MAGIC_OFF;
  watchdog_hw->scratch[3] = BOOT_MAGIC_OFF;
}

void enable_led(int pin) {
  gpio_put(pin, 0);
}
void disable_led(int pin) {
  gpio_put(pin, 1);
}

void turn_som_power_on() {
  printf("# [action] turn_som_power_on\n");
  init_spi_client();

  enable_led(PIN_LED_G);

  set_boot_magic();

  gpio_ext_enable(GPIO_EXT_3V3_EN);
  sleep_ms(10);
  gpio_ext_enable(GPIO_EXT_5V_EN);

  battery_info.som_is_powered = true;
}

void turn_som_power_off() {
  printf("# [action] turn_som_power_off\n");
  init_spi_client();

  disable_led(PIN_LED_G);

  clear_boot_magic();

  gpio_ext_disable(GPIO_EXT_5V_EN);
  gpio_ext_disable(GPIO_EXT_3V3_EN);

  battery_info.som_is_powered = false;
}

void som_wake()
{
  // TODO: toggle gpio!
  uart_puts(uart0, "wake\r\n");
}

// FIXME: move to utils
void i2c_scan(i2c_inst_t* i2c) {
  int id = 0;
  if (i2c == i2c1) id = 1;

  printf("\nI2C Scan (I2C %d)\n", id);
  printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

  for (int addr = 0; addr < (1 << 7); ++addr) {
    if (addr % 16 == 0) {
      printf("%02x ", addr);
    }

    int ret;
    uint8_t rxdata;
    ret = i2c_read_blocking(i2c, addr, &rxdata, 1, false);

    printf(ret < 0 ? "." : "@");
    printf(addr % 16 == 15 ? "\n" : "  ");
  }
}

void setup()
{
  tusb_init();
  reform_stdio_usb_init();

  // reset if main loop is stuck for 10 seconds
  watchdog_enable(10000, 1);

  // FIXME: gone with rp2350
  //printf("# [reset] cause: %#.8x\n", vreg_and_chip_reset_hw->chip_reset);
  //printf("# [reset] magic: %#.8lx%.8lx\n",
  //       watchdog_hw->scratch[2], watchdog_hw->scratch[3]);

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
  gpio_set_function(PIN_SOM_UART_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_SOM_UART_RX, GPIO_FUNC_UART);

  // I2C0
  gpio_set_function(PIN_SDA0, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL0, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA0, PIN_SCL0, GPIO_FUNC_I2C));
  i2c_init(i2c0, 100 * 1000);
  battery_info.packs[0].id = 0;
  battery_info.packs[0].i2c = i2c0;

  // I2C1
  gpio_set_function(PIN_SDA1, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL1, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA1, PIN_SCL1, GPIO_FUNC_I2C));
  i2c_init(i2c1, 100 * 1000);
  battery_info.packs[1].id = 1;
  battery_info.packs[1].i2c = i2c1;

  // motherboard external GPIOS
  gpio_ext_setup();

  /*while (true) {
    printf(".");
    sleep_ms(1000);
    i2c_scan(i2c1);
    }*/

  // RGB LED
  gpio_init(PIN_LED_R);
  gpio_init(PIN_LED_G);
  gpio_init(PIN_LED_B);
  gpio_set_dir(PIN_LED_R, 1);
  gpio_set_dir(PIN_LED_G, 1);
  gpio_set_dir(PIN_LED_B, 1);

  // Turn off RGB LED
  gpio_put(PIN_LED_R, 1);
  gpio_put(PIN_LED_G, 1);
  gpio_put(PIN_LED_B, 1);

  init_spi_client();

  // FIXME this is now on (usb-c) gpio extender
  // USB charger-port power rail
  //gpio_init(PIN_USB_SRC_ENABLE);
  //gpio_set_dir(PIN_USB_SRC_ENABLE, 1);
  //gpio_put(PIN_USB_SRC_ENABLE, 0);

  // if this is a warm boot, then we need to avoid latching the PWR and display
  // pins.
  if (syscon_warm_boot())
  {
    // on by default after reboot
    printf("# [reset] watchdog scratch had valid on magic, restoring power.\n");
    turn_som_power_on();
  }
  else
  {
    // off by default
    turn_som_power_off();
  }

  pd_init();
  charger_init();
}

void handle_usb_commands()
{
  int usb_c = getchar_timeout_us(0);
  if (usb_c != PICO_ERROR_TIMEOUT && isprint(usb_c))
  {
    printf("# [acm_command] '%c'\n", usb_c);
    if (usb_c == '1')
    {
      turn_som_power_on(true);
    }
    else if (usb_c == '0')
    {
      turn_som_power_off();
    }
    else if (usb_c == 'p')
    {
      battery_info.print_pack_info = !battery_info.print_pack_info;
    }
    else if (usb_c == 'i') {
      i2c_scan(i2c0);
    }
    else if (usb_c == 'I') {
      i2c_scan(i2c1);
    }
  }
}

void usb_host_5v_enable() {
  // TODO
}

void usb_host_5v_disable() {
  // TODO
}

#define BATTERY_TIMER_MS 1000

int64_t battery_task(__unused alarm_id_t id, __unused void *user_data) {
  pack_configure(&battery_info.packs[0], (float)BATTERY_TIMER_MS);
  pack_configure(&battery_info.packs[1], (float)BATTERY_TIMER_MS);
  charger_configure(battery_info.packs);
  charger_status(battery_info.packs);

  return BATTERY_TIMER_MS*1000;
}

bool spi_commands_task(__unused struct repeating_timer *t) {
  // handle commands from SoM
  handle_spi_commands(&battery_info);
  // timer should continue calling us
  return true;
}

void loop()
{
  bool can_sleep = true;

  // feed watchdog reset
  watchdog_update();

  // handle commands from keyboard
  handle_uart_commands(&battery_info);

#ifdef ACM_ENABLED
  // handle commands over usb serial
  handle_usb_commands();
#endif

  irq_set_enabled(ALARM_IRQ, false);
  if (!pd_tick(&battery_info)) {
    can_sleep = false;
  }
  irq_set_enabled(ALARM_IRQ, true);

  battery_info.ticks++;

  // every 1000ms: report to serial
  if (battery_info.ticks % 1000 == 0)
  {
    printf("1000 ticks...\n");
  }

  if (can_sleep) {
    sleep_us(100); // one tick is 0.1ms
  }
}

void mntre_reset_callback(void) {
  // TODO
}

int main()
{
  setup();

  sleep_ms(1000);
  printf("# [next_sysctl] sleep before main loop\n");
  sleep_ms(1000);

  ALARM_IRQ = timer_hardware_alarm_get_irq_num(timer_hw, 2);

  // call SPI task every 5ms to ensure response time
  struct repeating_timer spi_timer;
  add_repeating_timer_ms(-5, spi_commands_task, NULL, &spi_timer);

  // call configure task every few seconds
  // but at a lower priority than i.e. USB
  // via https://github.com/raspberrypi/pico-sdk/issues/751#issuecomment-1062078338
  __unused alarm_pool_t* alarm_pool = alarm_pool_create(2, 16); // create an alarm pool, use hardware alarm #2
  irq_set_priority(ALARM_IRQ, 0xc0); // larger number is lower priority
  alarm_pool_add_alarm_in_ms(alarm_pool, BATTERY_TIMER_MS, battery_task, NULL, false);

  printf("# [next_sysctl] entering main loop\n");

  while (true)
  {
    loop();
  }

  return 0;
}
