/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH

  MNT Reform Next: Hardware Command Interface
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "cli.h"
#include "machine.h"
#include "next_mux.h"
#include "forward_uart.h"

static struct machine *mach;

#define LEGACY_BUF_SZ 128
#define LEGACY_SPI_SZ 8
static char legacy_buf[LEGACY_BUF_SZ];
static char legacy_spi_buf[LEGACY_SPI_SZ];

uint64_t hwapi_legacy_q() {
  // execute status query command
  memset(legacy_spi_buf, 0, LEGACY_SPI_SZ);
  uint8_t percentage = (uint8_t)mach->charge_percentage;
  int16_t volts_int = (int16_t)(mach->battery_volts * 1000.0);
  int16_t current_int = (int16_t)(mach->battery_amps * 1000.0);

  legacy_spi_buf[0] = (uint8_t)volts_int;
  legacy_spi_buf[1] = (uint8_t)(volts_int >> 8);
  legacy_spi_buf[2] = (uint8_t)current_int;
  legacy_spi_buf[3] = (uint8_t)(current_int >> 8);
  legacy_spi_buf[4] = (uint8_t)percentage;
  legacy_spi_buf[5] = (uint8_t)0;
  return eightcc(legacy_spi_buf);
}

uint64_t hwapi_legacy_v() {
  // TODO: this isn't very useful?
  // get cell voltage
  memset(legacy_buf, 0, LEGACY_SPI_SZ);
  // pack 0
  struct battery_pack *pack = &mach->packs[0];
  int mv = pack->cells_v[0] * 1000;
  legacy_spi_buf[0] = (uint8_t)mv;
  legacy_spi_buf[1] = (uint8_t)(mv >> 8);
  return eightcc(legacy_spi_buf);
}

uint64_t hwapi_legacy_c() {
  // get calculated capacity (emulated)
  memset(legacy_buf, 0, LEGACY_SPI_SZ);
  uint16_t cap_accu = (uint16_t)charger_get_total_capacity_mah(mach) * (((float)mach->charge_percentage) / 100.0);
  uint16_t cap_min = (uint16_t)0;
  uint16_t cap_max = (uint16_t)charger_get_total_capacity_mah(mach);
  legacy_spi_buf[0] = (uint8_t)cap_accu;
  legacy_spi_buf[1] = (uint8_t)(cap_accu >> 8);
  legacy_spi_buf[2] = (uint8_t)cap_min;
  legacy_spi_buf[3] = (uint8_t)(cap_min >> 8);
  legacy_spi_buf[4] = (uint8_t)cap_max;
  legacy_spi_buf[5] = (uint8_t)(cap_max >> 8);
  return eightcc(legacy_spi_buf);
}

char* hwapi_legacy_kbd_s() {
  return (char*)"MNT Reform Next SC" MNTRE_FIRMWARE_VERSION "\r\n";
}

char* hwapi_legacy_kbd_c() {
  int ma = (int)(mach->battery_amps * 1000.0);
  char ma_sign = ' ';
  if (ma < 0) {
    ma = -ma;
    ma_sign = '-';
  }
  int mv = (int)(mach->battery_volts * 1000.0);
  snprintf(legacy_buf, 128,
           "%02d %02d %02d %02d %02d %02d %02d %02d mA%c%04dmV%05d %3d%% P%d\r\n",
           (int)(mach->packs[0].cells_v[0] * 10),
           (int)(mach->packs[0].cells_v[1] * 10),
           (int)(mach->packs[0].cells_v[2] * 10),
           (int)(mach->packs[0].cells_v[3] * 10),
           (int)(mach->packs[1].cells_v[0] * 10),
           (int)(mach->packs[1].cells_v[1] * 10),
           (int)(mach->packs[1].cells_v[2] * 10),
           (int)(mach->packs[1].cells_v[3] * 10),
	   ma_sign, ma, mv,
           mach->charge_percentage,
           mach->som_is_powered ? 1 : 0);

  legacy_buf[127] = 0;
  return (char*)legacy_buf;
}

uint64_t hwapi_set_backlight([[maybe_unused]] struct cli_context* ctx, uint64_t brightness) {
  // TODO
  return brightness;
}

uint64_t hwapi_set_backlight_freq([[maybe_unused]] struct cli_context* ctx, uint64_t freq) {
  // TODO
  return freq;
}

uint64_t hwapi_set_rail([[maybe_unused]] struct cli_context* ctx, uint64_t rail, uint64_t state) {
  if (rail == 0) {
    if (state == 0) {
      turn_som_power_off(mach);
    } else {
      turn_som_power_on(mach);
    }
  }
  return state;
}

// TODO
uint64_t hwapi_set_gpio([[maybe_unused]] struct cli_context* ctx, uint64_t id, uint64_t high) {
  switch (id) {
  case 0: {
    // 0: Display Panel Reset (active low)
    // N/A on Next
    break;
  }
  case 1: {
    break;
  }
  case 2: {
    break;
  }
  case 3: {
    break;
  }
  case 4: {
    break;
  }
  case 5: {
    break;
  }
  case 6: {
    usb_host_5v_set(high);
    break;
  }
  case 7: {
    break;
  }
  case 8: {
    break;
  }
  case 9: {
    break;
  }
  }
  return high;
}

uint64_t hwapi_set_usb_mode([[maybe_unused]] struct cli_context* ctx, uint64_t port, uint64_t mode) {
  // toggle USB muxing modes
  mux_set_usb_mode(port, mode);
  return 1;
}

uint64_t hwapi_set_usb_ports_sysctl([[maybe_unused]] struct cli_context* ctx) {
  // expose Sysctl on port 1 (charging port)
  hwapi_set_usb_mode(ctx, 0, 0);
  return 1;
}

uint64_t hwapi_set_usb_ports_host([[maybe_unused]] struct cli_context* ctx) {
  // set all ports to host mode
  hwapi_set_usb_mode(ctx, 0, 3);
  return 1;
}

uint64_t hwapi_set_usb_ports_edl([[maybe_unused]] struct cli_context* ctx) {
  // expose EDL mode on port 1 (charging port)
  hwapi_set_usb_mode(ctx, 0, 2);
  return 1;
}

uint64_t hwapi_set_usb_ports_uart([[maybe_unused]] struct cli_context* ctx) {
  // expose SoC UART on port 1 (charging port)
  hwapi_set_usb_mode(ctx, 0, 1);
  return 1;
}

uint64_t hwapi_soc_wake([[maybe_unused]] struct cli_context* ctx) {
  som_wake();
  return 1;
}

/* prepare SoC suspend by turning off unneeded power sources */
uint64_t hwapi_soc_pre_suspend([[maybe_unused]] struct cli_context* ctx) {
  // TODO
  return 1;
}

uint64_t hwapi_soc_post_suspend([[maybe_unused]] struct cli_context* ctx) {
  // TODO
  return 1;
}

uint64_t hwapi_get_cell_mv([[maybe_unused]] struct cli_context* ctx, uint64_t cell_id) {
  if (cell_id > 7) return 0;
  
  struct battery_pack *pack = &mach->packs[0];
  if (cell_id >= 4) pack = &mach->packs[1];
  int mv = pack->cells_v[cell_id % 4] * 1000;
  return mv;
}

uint64_t hwapi_get_pack_mv([[maybe_unused]] struct cli_context* ctx /*uint64_t pack_id*/) {
  // TODO what about pack_volts?
  return mach->battery_volts * 1000;
}

uint64_t hwapi_get_pack_ma([[maybe_unused]] struct cli_context* ctx /*uint64_t pack_id*/) {
  return mach->battery_amps * 1000;
}

uint64_t hwapi_get_pack_charge([[maybe_unused]] struct cli_context* ctx /*uint64_t pack_id*/) {
  return mach->charge_percentage;
}

uint64_t hwapi_get_cell_max_mah([[maybe_unused]] struct cli_context* ctx, [[maybe_unused]] uint64_t cell_id) {
  return mach->cell_max_mah;
}

uint64_t hwapi_get_sys_mv([[maybe_unused]] struct cli_context* ctx) {
  // TODO
  return mach->input_volts;
}

uint64_t hwapi_get_sys_ma([[maybe_unused]] struct cli_context* ctx) {
  // TODO
  // also: input amps?
  return 0;
}

uint64_t hwapi_get_wdog_scratch([[maybe_unused]] struct cli_context* ctx, uint64_t idx) {
  if (idx > 8) {
    return 0;
  }
  return (uint64_t)watchdog_hw->scratch[idx];
}

uint64_t hwapi_set_pack_debug([[maybe_unused]] struct cli_context* ctx, uint64_t on) {
  if (on > 1) on = 1;
  // FIXME leaky
  mach->print_pack_info = on;
  mach->packs[0].debug = on;
  mach->packs[1].debug = on;
  return on;
}

// TODO port new PD code
void hwapi_vdm([[maybe_unused]] struct cli_context* ctx, [[maybe_unused]] uint64_t message_type, [[maybe_unused]] uint64_t prime) {
  //send_vdm(message_type, prime);
}

void hwapi_vdm2([[maybe_unused]] struct cli_context* ctx, [[maybe_unused]] uint64_t obj0, [[maybe_unused]] uint64_t obj1) {
  //send_vdm2(obj0, obj1);
}

void hwapi_pd_cap([[maybe_unused]] struct cli_context* ctx, [[maybe_unused]] uint64_t prime) {
  //send_source_cap(prime);
}

void hwapi_pd_dr_swap([[maybe_unused]] struct cli_context* ctx) {
  //pd_send_dr_swap();
}

void hwapi_pd_pr_swap([[maybe_unused]] struct cli_context* ctx) {
  //pd_send_pr_swap();
}

void hwapi_pd_send_reset([[maybe_unused]] struct cli_context* ctx) {
  //pd_send_reset();
}

void hwapi_pd_set_max_voltage([[maybe_unused]] struct cli_context* ctx, [[maybe_unused]] uint64_t v) {
  //pd_set_max_voltage_req(v);
}

void hwapi_pd_set_force_sink([[maybe_unused]] struct cli_context* ctx, [[maybe_unused]] uint64_t force) {
  //pd_set_force_sink(!!force);
}

void hwapi_set_charge_current([[maybe_unused]] struct cli_context* ctx, uint64_t charge_ma) {
  if (charge_ma < 50) charge_ma = 50;
  if (charge_ma > 2000) charge_ma = 2000;
  charger_set_charge_current(mach, charge_ma);
}

void hwapi_pack_init([[maybe_unused]] struct cli_context* ctx, uint64_t pack_id) {
  if (pack_id == 0) {
    battery_pack_setup(i2c0);
  } else if (pack_id == 1) {
    battery_pack_setup(i2c1);
  }
}

void hwapi_set_uart_forwarding([[maybe_unused]] struct cli_context* ctx, uint64_t on) {
  // TODO global mode flags?
  set_forward_uart_mode(!!on);
}

void hwapi_init(struct machine *mach_) {
  mach = mach_;

  /* register all available functions */
  cli_add_func("set-rail", hwapi_set_rail, 2, CLI_TYPE_UINT64);
  cli_add_func("set-gpio", hwapi_set_gpio, 2, CLI_TYPE_UINT64);
  cli_add_func("set-usb\0", hwapi_set_usb_mode, 2, CLI_TYPE_UINT64);
  cli_add_func("usb-sc\0\0", hwapi_set_usb_ports_sysctl, 0, CLI_TYPE_UINT64);
  cli_add_func("usb-edl\0", hwapi_set_usb_ports_edl, 0, CLI_TYPE_UINT64);
  cli_add_func("usb-host", hwapi_set_usb_ports_host, 0, CLI_TYPE_UINT64);
  cli_add_func("usb-uart", hwapi_set_usb_ports_uart, 0, CLI_TYPE_UINT64);
  cli_add_func("set-lite", hwapi_set_backlight, 1, CLI_TYPE_UINT64);
  cli_add_func("set-lfrq", hwapi_set_backlight_freq, 1, CLI_TYPE_UINT64);
  cli_add_func("set-cma\0", hwapi_set_charge_current, 1, CLI_TYPE_UINT64);
  cli_add_func("cell-mv\0", hwapi_get_cell_mv, 1, CLI_TYPE_UINT64);
  cli_add_func("cell-mah", hwapi_get_cell_max_mah, 1, CLI_TYPE_UINT64);
  cli_add_func("pack-mv\0", hwapi_get_pack_mv, 1, CLI_TYPE_UINT64);
  cli_add_func("pack-ma\0", hwapi_get_pack_ma, 1, CLI_TYPE_UINT64);
  cli_add_func("pack-crg", hwapi_get_pack_charge, 1, CLI_TYPE_UINT64);
  cli_add_func("pack-dbg", hwapi_set_pack_debug, 1, CLI_TYPE_UINT64);
  cli_add_func("pack-init", hwapi_pack_init, 1, CLI_TYPE_UINT64);
  cli_add_func("sys-mv\0\0", hwapi_get_sys_mv, 0, CLI_TYPE_UINT64);
  cli_add_func("sys-ma\0\0", hwapi_get_sys_ma, 0, CLI_TYPE_UINT64);
  cli_add_func("soc-wake", hwapi_soc_wake, 0, CLI_TYPE_UINT64);
  cli_add_func("soc-susp", hwapi_soc_pre_suspend, 0, CLI_TYPE_UINT64);
  cli_add_func("soc-psus", hwapi_soc_post_suspend, 0, CLI_TYPE_UINT64);
  cli_add_func("uart-fwd", hwapi_set_uart_forwarding, 1, CLI_TYPE_VOID);
  cli_add_func("pwrsave\0", enter_powersave, 0, CLI_TYPE_VOID);
  cli_add_func("vdm\0\0\0\0\0", hwapi_vdm, 2, CLI_TYPE_VOID);
  cli_add_func("vdm2\0\0\0\0", hwapi_vdm2, 2, CLI_TYPE_VOID);
  cli_add_func("pdcap\0\0\0", hwapi_pd_cap, 0, CLI_TYPE_VOID);
  cli_add_func("pdreset\0", hwapi_pd_send_reset, 0, CLI_TYPE_VOID);
  cli_add_func("pdvolt\0\0", hwapi_pd_set_max_voltage, 1, CLI_TYPE_VOID);
  cli_add_func("pdsink\0\0", hwapi_pd_set_force_sink, 1, CLI_TYPE_VOID);
  cli_add_func("pdprswap\0\0", hwapi_pd_pr_swap, 0, CLI_TYPE_VOID);
  cli_add_func("pddrswap\0\0", hwapi_pd_dr_swap, 0, CLI_TYPE_VOID);
  cli_add_func("wdog-scr\0\0", hwapi_get_wdog_scratch, 2, CLI_TYPE_UINT64);

  // TODO?
  // gpio_set_dir(PIN_USB_LOADER_SW, GPIO_OUT);
  // gpio_put(PIN_USB_LOADER_SW, 1);
  // serial forwarding (next)

  /* register constants */
  cli_add_word("mb-ver\0\0", 1);
  cli_add_word("pack-cnt", 2);
  cli_add_word("cell-cnt", 8);

  /* legacy API commands */
  cli_add_func("0p\0\0\0\0\0\0", turn_som_power_off, 0, CLI_TYPE_VOID);
  cli_add_func("1p\0\0\0\0\0\0", turn_som_power_on, 0, CLI_TYPE_VOID);
  cli_add_func("0q\0\0\0\0\0\0\0", hwapi_legacy_q, 0, CLI_TYPE_UINT64);
  cli_add_func("0v\0\0\0\0\0\0\0", hwapi_legacy_v, 0, CLI_TYPE_UINT64);
  cli_add_func("0c\0\0\0\0\0\0\0", hwapi_legacy_c, 0, CLI_TYPE_UINT64);
  cli_add_func("1w\0\0\0\0\0\0\0", som_wake, 0, CLI_TYPE_VOID);
  cli_add_word("0f\0\0\0\0\0\0\0", eightcc("MNT RNSC"));
  cli_add_word("1f\0\0\0\0\0\0\0", eightcc("20260908"));
  cli_add_word("2f\0\0\0\0\0\0\0", eightcc("00000000"));
  cli_add_func("s\0\0\0\0\0\0\0", hwapi_legacy_kbd_s, 0, CLI_TYPE_STR128);
  cli_add_func("c\0\0\0\0\0\0\0", hwapi_legacy_kbd_c, 0, CLI_TYPE_STR128);
}
