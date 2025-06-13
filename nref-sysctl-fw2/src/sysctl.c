/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Pocket Reform System Controller Firmware for RP2040
  Copyright 2023-2024 MNT Research GmbH

  fusb_read/write functions based on:
  https://git.clarahobbs.com/pd-buddy/pd-buddy-firmware/src/branch/master/lib/src/fusb302b.c
*/
#include "sysctl.h"
#include "pico/divider.h"
#include "tusb.h"
#include "reform_stdio_usb.h"

battery_info_s battery_info = {0};
int disp_bl_percent = 100;

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

void charger_tick();

void charger_init()
{
  // reset all registers

  // turn off charging until PD allows it

  //mps_read_buf(MPS_REGSTART_CONFIG, sizeof(mps_reg_config.all_regs), mps_reg_config.all_regs);
  //mps_read_buf(MPS_REGSTART_LIMITS, sizeof(mps_reg_limits.all_regs), mps_reg_limits.all_regs);
  //mps_read_buf(MPS_REGSTART_STATUS, sizeof(mps_reg_status.all_regs), mps_reg_status.all_regs);

  // 2A max charge current, assumes 4000mAh cells.
  // will be written into register by charger_disable_charge.
  //mps_reg_limits.charge_current = 1<<5 | 1<<3;

  // TODO
  //charger_disable_charge();

  // see https://www.ti.com/lit/ds/symlink/bq25792.pdf
  // VREG = charge voltage

  bq25792_write_byte(0x00, (10000 - 2500) / 250); // 10.0V vsysmin, 250mV step, 2500mV offset
  bq25792_write_word(0x01, 14800 / 10); // charge voltage (conservative), VREG
  bq25792_write_word(0x03, 3000 / 10); // charge current
  bq25792_write_word(0x06, 3000 / 10); // input current, defaults to 3A @ reset (60W)

  // ADC control: 0x2e (default: 0x30)
  bq25792_write_byte(0x2e, (1<<7) | (0b00 << 4) ); // enable ADC at 15 bit (7=ADC_EN, 5:4=ADC_SAMPLE)

  // default IOTG setting (3000mA)
  bq25792_write_byte(0x0d, 0b01001011);

  // charger_control_2
  // bit6: AUTO_INDET_EN (default on, D+/D- detection)
  bq25792_write_byte(0x11, 0b00000000);

  // charger_control_5
  // disable EXTILIM
  // enable IBAT discharge current sensing
  bq25792_write_byte(0x14, 0b00111100);

  charger_tick();
}

void charger_tick() {
  // TODO
}

// current in 10mA units
void charger_enable_charge(int current) {
  int current_reg_value = current / 5;
  printf("# [charger] setting limit %d \n", current_reg_value);

  mps_reg_limits.input_i_limit1 = current_reg_value;

  gpio_put(PIN_LED_R, 1);
}

void charger_disable_charge() {
  // TODO: set all current limits to 500mA (should always be safe)

  gpio_put(PIN_LED_R, 0);
}

void pack_tick(battery_info_s *battery_info)
{
  battery_info->charge_percentage = (int)rep_percentage;
  // charger mostly doesn't charge to >98%
  if (battery_info->charge_percentage >= 98)
  {
    battery_info->charge_percentage = 100;
  }
  battery_info->cell1_volts = cell1;
  battery_info->cell2_volts = cell2;
  battery_info->time_to_empty = rep_time_to_empty;

  if (battery_info->print_pack_info) {
    printf("[pack_info]\n");
  }
}

void pack_init() {
  gauge_tick(&battery_info);
}

void charger_dump(battery_info_s *battery_info)
{
  // carry over to globals for SPI reporting
  battery_info->battery_amps = -(float)(adc_input_i - adc_discharge_c)/(float)1000.0;
  battery_info->battery_volts = (float)adc_sys_v/(float)1000.0;
  battery_info->input_volts = adc_input_v;

  if (battery_info->print_pack_info) {
  }
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

  som_is_powered = false;
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
  init_spi_client();

  // FIXME: gone with rp2350
  //printf("# [reset] cause: %#.8x\n", vreg_and_chip_reset_hw->chip_reset);
  printf("# [reset] magic: %#.8x%.8x\n",
         watchdog_hw->scratch[2], watchdog_hw->scratch[3]);

  // UART to keyboard
  uart_init(UART_ID, BAUD_RATE);
  uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
  uart_set_hw_flow(UART_ID, false, false);
  uart_set_fifo_enabled(UART_ID, true);
  gpio_set_function(PIN_KBD_UART_TX, GPIO_FUNC_UART);
  gpio_set_function(PIN_KBD_UART_RX, GPIO_FUNC_UART);
  int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

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
  packs[0].id = 0;
  packs[0].i2c = i2c0;

  // I2C1
  gpio_set_function(PIN_SDA1, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL1, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA1, PIN_SCL1, GPIO_FUNC_I2C));
  i2c_init(i2c1, 100 * 1000);
  packs[1].id = 1;
  packs[1].i2c = i2c1;

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

  gauge_init();
  charger_init();

  pd_init();
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
#ifndef OTG_AS_5V
  gpio_put(PIN_USB_SRC_ENABLE, 1);
#else
  mps_reg_config.config0.otg_en = 1;
  mps_write_byte(MPS_REG_CONFIG0, mps_reg_config.config0.reg_byte);
#endif
}

void usb_host_5v_disable() {
#ifndef OTG_AS_5V
  gpio_put(PIN_USB_SRC_ENABLE, 0);
#else
  mps_reg_config.config0.otg_en = 0;
  mps_write_byte(MPS_REG_CONFIG0, mps_reg_config.config0.reg_byte);
#endif
}

void loop()
{
  bool can_sleep = true;

  // handle commands from keyboard
  handle_uart_commands(&battery_info);

#ifdef ACM_ENABLED
  // handle commands over usb serial
  handle_usb_commands();
#endif

  if (!pd_tick(&battery_info)) {
    can_sleep = false;
  }
  charger_tick();

  battery_info.ticks++;

  // every 100ms: query gauge and charger, update battery status
  if (battery_info.ticks % 1000 == 0)
  {
  }

  // every 1000ms: report to serial
  if (battery_info.ticks % 10000 == 0)
  {
    // TODO: print adc_charge_c adc_discharge_c
    printf("# %s %s %s chg=%1x mps_flt=%02x input=%dmV@%dmA charge=%dmA discharge=%dmA p=%0.2fW ttempty=%umin\n",
            battery_info.som_is_powered ? "ON" : "OFF",
            mps_reg_status.status.acok ? "AC" : "BAT",
            mps_reg_config.config0.chg_en ? "CHG" : "",
            mps_reg_status.status.chg_stat,
            mps_reg_status.fault.reg_byte,
            mps_word_to_12800(mps_reg_adc.input_v),
            mps_word_to_3200(mps_reg_adc.input_i),
            mps_word_to_6400(mps_reg_adc.bat_charge_i),
            mps_word_to_6400(mps_reg_adc.bat_discharge_i),
            mps_word_to_watt(mps_reg_adc.sys_p),
            (unsigned int)battery_info.time_to_empty/60
            );
  }

  if (can_sleep) {
    sleep_us(100); // one tick is 0.1ms
  }
}

void mntre_reset_callback(void) {
  // TODO
}

bool spi_commands_task(__unused struct repeating_timer *t) {
  charger_configure();
  pack_configure(&packs[0], (float)ms_elapsed);
  pack_configure(&packs[1], (float)ms_elapsed);
  input_mv = charger_status();
  //gauge_tick(&battery_info);
  //charger_dump(&battery_info);
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
  add_repeating_timer_ms(-1000, battery_timer, NULL, &battery_timer);

  printf("# [next_sysctl] entering main loop\n");

  while (true)
  {
    loop();
  }

  return 0;
}
