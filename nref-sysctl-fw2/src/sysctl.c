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
static alarm_pool_t* battery_alarm_pool;
static int charge_ma = 500;
battery_info_s battery_info = {0};
static int ALARM_IRQ = 0;
static int console_forward_mode = 0;

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

  enable_led(PIN_LED_B);
  gpio_ext_pd_poweron_defaults();

  set_boot_magic();

  gpio_mb_enable(GPIO_EXT_3V3_EN);
  gpio_mb_enable(GPIO_EXT_5V_EN);

  battery_info.som_is_powered = true;
}

/*
  this function can be called from a timer interrupt
  in the spi command handler, no sleep is allowed here.
  if delays should become necessary, they have to be
  busy loops.
*/
void turn_som_power_off() {
  printf("# [action] turn_som_power_off\n");

  disable_led(PIN_LED_B);
  gpio_ext_pd_poweroff_defaults();

  clear_boot_magic();

  gpio_mb_disable(GPIO_EXT_5V_EN);
  gpio_mb_disable(GPIO_EXT_3V3_EN);

  battery_info.som_is_powered = false;
}

void som_wake()
{
  gpio_put(PIN_SOM_WAKE, 1);
  sleep_ms(5);
  gpio_put(PIN_SOM_WAKE, 0);
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
  // used for stdio and reset interface
  tusb_init();
  reform_stdio_usb_init();

  // reset if main loop is stuck for 10 seconds
  watchdog_enable(10000, 1);

  // FIXME: gone/moved with rp2350
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
  battery_info.packs[0].id = 0;
  battery_info.packs[0].i2c = i2c0;

  // I2C1
  gpio_set_function(PIN_SDA1, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL1, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA1, PIN_SCL1, GPIO_FUNC_I2C));
  i2c_init(i2c1, 100 * 1000);
  battery_info.packs[1].id = 1;
  battery_info.packs[1].i2c = i2c1;

  // motherboard external GPIOs
  gpio_mb_setup();
  // left port board (PD) GPIOs
  gpio_ext_pd_setup();

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

  // SoM / SoC wake GPIO
  gpio_init(PIN_SOM_WAKE);
  gpio_set_dir(PIN_SOM_WAKE, GPIO_OUT);
  gpio_put(PIN_SOM_WAKE, 0);

  init_spi_client();

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
      turn_som_power_on();
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
    else if (usb_c == 'd') {
      battery_info.packs[0].debug = 1;
      battery_info.packs[1].debug = 1;
      bq25792_set_debug(1);
    }
    else if (usb_c == 'D') {
      battery_info.packs[0].debug = 0;
      battery_info.packs[1].debug = 0;
      bq25792_set_debug(0);
    }
    else if (usb_c == 'u') {
      pd_init();
    }
    else if (usb_c == 'c') {
      monitor_setup(i2c0);
    }
    else if (usb_c == 'C') {
      monitor_setup(i2c1);
    }
    else if (usb_c == 'f') {
      //mon_fet_test(i2c0);
    }
    else if (usb_c == 'F') {
      //mon_toggle_fet_en(i2c0);
    }
    else if (usb_c == '+') {
      charge_ma += 100;
      if (charge_ma > 2000) charge_ma = 2000;
      charger_set_charge_current(charge_ma); // charge current
    }
    else if (usb_c == '-') {
      charge_ma -= 100;
      if (charge_ma < 50) charge_ma = 50;
      charger_set_charge_current(charge_ma);
    }
    else if (usb_c == 's') {
      printf("turning on USB PD 5V source...\n");
      gpio_ext_pd_usb_5v_src_set(1);
    }
    else if (usb_c == 'S') {
      printf("turning off USB PD 5V source...\n");
      gpio_ext_pd_usb_5v_src_set(0);
    }
    else if (usb_c == '5') {
      printf("turning on USB PD AUX 5V...\n");
      gpio_ext_pd_enable(7);
    }
    else if (usb_c == '%') {
      printf("turning off USB PD AUX 5V...\n");
      gpio_ext_pd_disable(7);
    }
    else if (usb_c == 'm') {
      printf("setting USB-C mux dir to 0...\n");
      gpio_ext_pd_disable(2);
    }
    else if (usb_c == 'M') {
      printf("setting USB-C mux dir to 1...\n");
      gpio_ext_pd_enable(2);
    }
    else if (usb_c == '4') {
      // configuration for USB-UART (SoC console), and EDL providing USB hub upstream
      // sysctl reachable via SoC USB
      printf("setting USWITCH_1 to 1...\n");
      gpio_mb_enable(4);
      printf("setting USWITCH_2 to 1...\n");
      gpio_mb_enable(5);
      printf("setting USWITCH_3 to 1...\n");
      gpio_mb_enable(6);

      // turn off sending to SoC UART
      gpio_init(PIN_SOM_UART_TX);
      gpio_set_dir(PIN_SOM_UART_TX, 0);
    }
    else if (usb_c == '5') {
      // configuration for SysCtl flashing (default)
      printf("setting USWITCH_1 to 0...\n");
      gpio_mb_disable(4);
      printf("setting USWITCH_2 to 0...\n");
      gpio_mb_disable(5);
      printf("setting USWITCH_3 to 0...\n");
      gpio_mb_disable(6);
    }
    else if (usb_c == '6') {
      // configuration for EDL port on USB-C
      // no going back from this via USB, sysctl unreachable except for SPI
      printf("setting USWITCH_1 to 1...\n");
      gpio_mb_enable(4);
      printf("setting USWITCH_2 to 0...\n");
      gpio_mb_disable(5);
      printf("setting USWITCH_3 to 0...\n");
      gpio_mb_disable(6);
    }
    else if (usb_c == '7') {
      // configuration for normal USB-C and SysCtl as USB device of SoC
      // sysctl reachable via SoC USB
      printf("setting USWITCH_1 to 1...\n");
      gpio_mb_enable(4);
      printf("setting USWITCH_2 to 0...\n");
      gpio_mb_disable(5);
      printf("setting USWITCH_3 to 1...\n");
      gpio_mb_enable(6);
    }
    else if (usb_c == '/') {
      console_forward_mode = 1;
      gpio_set_function(PIN_SOM_UART_TX, GPIO_FUNC_UART);
      printf("\n--- entered console forward mode ---\n");
    }
  }
}

void usb_host_5v_enable() {
  gpio_ext_pd_usb_5v_src_set(1);
}

void usb_host_5v_disable() {
  gpio_ext_pd_usb_5v_src_set(0);
}

#define BATTERY_TIMER_MS 2000

int64_t battery_task(__unused alarm_id_t id, __unused void *user_data) {
  pack_configure(&battery_info.packs[0], (float)BATTERY_TIMER_MS);
  pack_configure(&battery_info.packs[1], (float)BATTERY_TIMER_MS);
  charger_configure();
  charger_status(battery_info.packs);

  return BATTERY_TIMER_MS*1000;
}

bool spi_commands_task(__unused struct repeating_timer *t) {
  // handle commands from SoM
  handle_spi_commands(&battery_info);
  // timer should continue calling us
  return true;
}

void forward_soc_uart() {
  // prevent endless loop
  int uart_max = 64;
  while (uart_is_readable(uart0) && uart_max > 0) {
    printf("%c", uart_getc(uart0));
    uart_max--;
  }
  int usb_c = getchar_timeout_us(0);
  if (usb_c != PICO_ERROR_TIMEOUT) {
    if (usb_c == 16) {
      // "data link escape", ctrl+p
      console_forward_mode = 0;
      printf("\n--- exited console forward mode ---\n");
      // turn off sending to SoC UART
      gpio_init(PIN_SOM_UART_TX);
      gpio_set_dir(PIN_SOM_UART_TX, 0);
    } else {
      uart_putc(uart0, usb_c);
    }
  }
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
  if (console_forward_mode) {
    forward_soc_uart();
  } else {
    handle_usb_commands();
  }
#endif

  irq_set_enabled(ALARM_IRQ, false);
  if (!pd_tick(&battery_info)) {
    can_sleep = false;
  }
  irq_set_enabled(ALARM_IRQ, true);
  battery_info.ticks++;

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

  sleep_ms(500);
  printf("# [next_sysctl] sleep before main loop\n");
  
  ALARM_IRQ = timer_hardware_alarm_get_irq_num(timer_hw, 2);

  // call SPI task every 5ms to ensure response time
  struct repeating_timer spi_timer;
  add_repeating_timer_ms(-5, spi_commands_task, NULL, &spi_timer);

  // call configure task every few seconds
  // but at a lower priority than i.e. USB
  // via https://github.com/raspberrypi/pico-sdk/issues/751#issuecomment-1062078338
  battery_alarm_pool = alarm_pool_create(2, 16); // create an alarm pool, use hardware alarm #2
  irq_set_priority(ALARM_IRQ, 0xc0); // larger number is lower priority
  alarm_pool_add_alarm_in_ms(battery_alarm_pool, BATTERY_TIMER_MS, battery_task, NULL, false);

  printf("# [next_sysctl] entering main loop\n");

  while (true)
  {
    loop();
  }

  return 0;
}
