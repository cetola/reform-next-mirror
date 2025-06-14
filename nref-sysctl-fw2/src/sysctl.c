/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2023-2025 MNT Research GmbH

  fusb_read/write functions based on:
  https://git.clarahobbs.com/pd-buddy/pd-buddy-firmware/src/branch/master/lib/src/fusb302b.c
*/
#include "sysctl.h"
#include "pico/divider.h"
#include "tusb.h"
#include "reform_stdio_usb.h"
#include "next_gpio.h"
#include "bq25792.h"
#include "bq76922.h"

// FIXME
battery_info_s battery_info = {0};

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

// current in 10mA units
void charger_enable_charge(int current) {
  int current_reg_value = current / 10;
  printf("# [charger] setting limit %d \n", current_reg_value);

  bq25792_write_word(0x03, current_reg_value); // charge current
  bq25792_write_word(0x06, current_reg_value); // input current, defaults to 3A @ reset (60W)

  gpio_put(PIN_LED_R, 1);
}

void charger_disable_charge() {
  // set all current limits to 500mA (should always be safe)
  bq25792_write_word(0x03, 500 / 10); // charge current
  bq25792_write_word(0x06, 500 / 10); // input current, defaults to 3A @ reset (60W)
  
  gpio_put(PIN_LED_R, 0);
}

void turn_som_power_on() {
  printf("# [action] turn_som_power_on\n");
  init_spi_client();

  gpio_put(PIN_LED_B, 1);

  set_boot_magic();

  gpio_ext_enable(GPIO_EXT_3V3_EN);
  sleep_ms(10);
  gpio_ext_enable(GPIO_EXT_5V_EN);

  battery_info.som_is_powered = true;
}

void turn_som_power_off() {
  printf("# [action] turn_som_power_off\n");
  init_spi_client();

  gpio_put(PIN_LED_B, 0);

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

void setup()
{
  tusb_init();
  reform_stdio_usb_init();

  // reset if main loop is stuck for 1000ms
  watchdog_enable(1000, 1);
  
  init_spi_client();

  // FIXME: gone with rp2350
  //printf("# [reset] cause: %#.8x\n", vreg_and_chip_reset_hw->chip_reset);
  printf("# [reset] magic: %#.8lx%.8lx\n",
         watchdog_hw->scratch[2], watchdog_hw->scratch[3]);

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

  // RGB LED
  gpio_init(PIN_LED_R);
  gpio_init(PIN_LED_G);
  gpio_init(PIN_LED_B);
  gpio_set_dir(PIN_LED_R, 1);
  gpio_set_dir(PIN_LED_G, 1);
  gpio_set_dir(PIN_LED_B, 1);

  // Turn off RGB LED
  gpio_put(PIN_LED_R, 0);
  gpio_put(PIN_LED_G, 0);
  gpio_put(PIN_LED_B, 0);

  // FIXME this is now on (usb-c) gpio extender
  // USB charger-port power rail
  //gpio_init(PIN_USB_SRC_ENABLE);
  //gpio_set_dir(PIN_USB_SRC_ENABLE, 1);
  //gpio_put(PIN_USB_SRC_ENABLE, 0);

  // motherboard external GPIOS
  gpio_ext_setup();

  // if this is a warm boot, then we need to avoid latching the PWR and display
  // pins.
  if (syscon_warm_boot())
  {
    // on by default after reboot
    printf("# [reset] watchdog scratch had valid on magic, restoring power.\n");
    battery_info.som_is_powered = true;
    turn_som_power_on();
  }
  else
  {
    // FIXME
    // off by default
    //turn_som_power_off();
  }

  charger_init();
  pd_init();
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

void handle_usb_commands()
{
  int usb_c = getchar_timeout_us(0);
  if (usb_c != PICO_ERROR_TIMEOUT && isprint(usb_c))
  {
    printf("# [acm_command] '%c'\n", usb_c);
    if (usb_c == '1')
    {
      if (battery_info.som_is_powered) {
        turn_som_power_on(true);
      } else {
        turn_som_power_on(false);
      }
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

  if (!pd_tick(&battery_info)) {
    can_sleep = false;
  }

  battery_info.ticks++;

  // every 1000ms: report to serial
  if (battery_info.ticks % 10000 == 0)
  {
  }

  if (can_sleep) {
    sleep_us(100); // one tick is 0.1ms
  }
}

void mntre_reset_callback(void) {
  // TODO
}

#define BATTERY_TIMER_MS 1000

bool battery_task(__unused struct repeating_timer *t) {
  charger_configure(battery_info.packs);
  pack_configure(&battery_info.packs[0], (float)BATTERY_TIMER_MS);
  pack_configure(&battery_info.packs[1], (float)BATTERY_TIMER_MS);
  charger_status(battery_info.packs);
  // timer should continue calling us
  return true;
}

bool spi_commands_task(__unused struct repeating_timer *t) {
  // handle commands from SoM
  handle_spi_commands(&battery_info);
  // timer should continue calling us
  return true;
}

int main()
{
  setup();

  // call SPI task every 5ms to ensure response time
  struct repeating_timer spi_timer;
  add_repeating_timer_ms(-5, spi_commands_task, NULL, &spi_timer);

  // call configure task every 1000ms to ensure response time
  struct repeating_timer battery_timer;
  add_repeating_timer_ms(-BATTERY_TIMER_MS, battery_task, NULL, &battery_timer);

  printf("# [next_sysctl] entering main loop\n");

  while (true)
  {
    loop();
  }

  return 0;
}
