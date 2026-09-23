/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2023-2026 MNT Research GmbH

  MNT Reform Next Battery Pack
 */

#include "machine_next.h"
#include <next_pack.h>
#include <stdio.h>

// BQ76922 monitor on battery boards
#define BQ76922_ADDR 0x08
#define I2C_TIMEOUT (1000*500)
#include <bq76922.h>

int battery_pack_task([[maybe_unused]] struct machine* mach, struct battery_pack *pack, [[maybe_unused]] float ms_elapsed) {
  // FIXME: this function itself shouldn't printf
  // as we're in an IRQ callback

  // subcommand: lo to 0x3e, hi to 0x3f
  // read 0x3e, 0x3f. if == 0xff, busy
  //                  if == written subcommand, done
  // read 0x61 response length
  // read 0x40... response length (max. up to 0x60)
  // later: check checksum (at 0x60). checksum includes 0x3e, 0x3f
  // don't read checksum and length at the same time (auto-increment stuff)

  // later, we can write defaults to OTP memory

  float time_real_ms = to_ms_since_boot(get_absolute_time());
  float elapsed_real_ms = time_real_ms - pack->time_last_ms;
  if (elapsed_real_ms < 1) elapsed_real_ms = 1; // protection against div by zero
  if (elapsed_real_ms > 10000) elapsed_real_ms = 10000; // clip
  pack->time_last_ms = time_real_ms;

  i2c_inst_t* i2c = pack->i2c;

  //printf("[pack %d] detecting...\n", pack->id);
  int detected = bq76922_detect(i2c);

  if (detected != 1) {
    //printf("[pack %d] not detected: %d\n", pack->id, detected);
    // pack not connected
    pack->cells_v[0] = 0;
    pack->cells_v[1] = 0;
    pack->cells_v[2] = 0;
    pack->cells_v[3] = 0;
    pack->undervolt = 0;
    pack->overvolt = 0;
    pack->active = false;
    pack->coulomb_zero = 0;
    pack->coulomb_cur = 0;
    pack->coulomb_max = 0;
    pack->gauge_percent = 0;
    pack->ms_at_rest = 0;
    return 0;
  } else {
    pack->active = true;
    // 2.0A x 3600 seconds/hour (pack capacity)
    pack->coulomb_max = 3600.0 * ((float)mach->cell_max_mah) / 1000.0;
  }

  // FIXME
  mon_sleep_on(i2c);
  //mon_sleep_off(i2c);

  //printf("[pack %d] reading...\n", pack->id);
  uint16_t cell1_mv_lo = bq76922_read_byte(i2c, 0x14);
  uint16_t cell1_mv_hi = bq76922_read_byte(i2c, 0x15);
  uint16_t cell2_mv_lo = bq76922_read_byte(i2c, 0x16);
  uint16_t cell2_mv_hi = bq76922_read_byte(i2c, 0x17);
  //uint16_t cell3_mv_lo = bq76922_read_byte(i2c, 0x18);
  //uint16_t cell3_mv_hi = bq76922_read_byte(i2c, 0x19);
  uint16_t cell4_mv_lo = bq76922_read_byte(i2c, 0x1a);
  uint16_t cell4_mv_hi = bq76922_read_byte(i2c, 0x1b);
  uint16_t cell5_mv_lo = bq76922_read_byte(i2c, 0x1c);
  uint16_t cell5_mv_hi = bq76922_read_byte(i2c, 0x1d);

  uint16_t stack_userv_lo = bq76922_read_byte(i2c, 0x34);
  uint16_t stack_userv_hi = bq76922_read_byte(i2c, 0x35);
  uint16_t pack_userv_lo = bq76922_read_byte(i2c, 0x36);
  uint16_t pack_userv_hi = bq76922_read_byte(i2c, 0x37);
  uint16_t ld_userv_lo = bq76922_read_byte(i2c, 0x38);
  uint16_t ld_userv_hi = bq76922_read_byte(i2c, 0x39);
  uint16_t cc2_usera_lo = bq76922_read_byte(i2c, 0x3a);
  uint16_t cc2_usera_hi = bq76922_read_byte(i2c, 0x3b);

  float cell1_mv = cell1_mv_lo|(cell1_mv_hi<<8);
  float cell2_mv = cell2_mv_lo|(cell2_mv_hi<<8);
  //float cell3_mv = cell3_mv_lo|(cell3_mv_hi<<8);
  float cell4_mv = cell4_mv_lo|(cell4_mv_hi<<8);
  float cell5_mv = cell5_mv_lo|(cell5_mv_hi<<8);
  float stack_cv = (int16_t)(stack_userv_lo|(stack_userv_hi<<8));
  float pack_mv = (int16_t)(pack_userv_lo|(pack_userv_hi<<8));
  float ld_mv = (int16_t)(ld_userv_lo|(ld_userv_hi<<8));
  float cc2_ma = -((int16_t)(cc2_usera_lo|(cc2_usera_hi<<8)));

  pack->cells_v[0] = cell1_mv;
  pack->cells_v[1] = cell2_mv;
  pack->cells_v[2] = cell4_mv;
  pack->cells_v[3] = cell5_mv;

  if (cell1_mv >= MV_FULL &&
      cell2_mv >= MV_FULL &&
      cell4_mv >= MV_FULL &&
      cell5_mv >= MV_FULL) {
    pack->fully_charged = 1;
  } else {
    if (cell1_mv <= (MV_FULL-MV_HYST) &&
        cell2_mv <= (MV_FULL-MV_HYST) &&
        cell4_mv <= (MV_FULL-MV_HYST) &&
        cell5_mv <= (MV_FULL-MV_HYST)) {
      pack->fully_charged = 0;
    }
  }

  if (cell1_mv >= MV_OVERVOLT ||
      cell2_mv >= MV_OVERVOLT ||
      cell4_mv >= MV_OVERVOLT ||
      cell5_mv >= MV_OVERVOLT) {
    pack->overvolt = 1;
  } else {
    if (cell1_mv <= (MV_OVERVOLT-MV_HYST) &&
        cell2_mv <= (MV_OVERVOLT-MV_HYST) &&
        cell4_mv <= (MV_OVERVOLT-MV_HYST) &&
        cell5_mv <= (MV_OVERVOLT-MV_HYST)) {
      pack->overvolt = 0;
    }
  }

  if (cell1_mv <= MV_UNDERVOLT ||
      cell2_mv <= MV_UNDERVOLT ||
      cell4_mv <= MV_UNDERVOLT ||
      cell5_mv <= MV_UNDERVOLT) {
    pack->undervolt = 1;
  } else {
    if (cell1_mv >= (MV_UNDERVOLT+MV_HYST) &&
        cell2_mv >= (MV_UNDERVOLT+MV_HYST) &&
        cell4_mv >= (MV_UNDERVOLT+MV_HYST) &&
        cell5_mv >= (MV_UNDERVOLT+MV_HYST)) {
      pack->undervolt = 0;
    }
  }

  if (pack->undervolt) {
    printf("# [bq76:%d] undervoltage, turning discharge off.\n", pack->id);
    mon_discharge_fets_off(i2c);
  } else if (pack->overvolt) {
    printf("# [bq76:%d] overvoltage, turning charge off.\n", pack->id);
    mon_charge_fets_off(i2c);
  } else {
    mon_all_fets_on(i2c);
  }

  pack->volt = stack_cv/100.0; // default unit is centivolts
  pack->ampere = cc2_ma/1000.0; // default unit is mA

  // coulomb counting (gauge) -------------------
  // 1C = 1A x 1s

  float coulomb = pack->ampere * (elapsed_real_ms / 1000.0);
  if (pack->debug) {
    float remain;
    if (coulomb < 0) {
      // charging, estimate time to full
      remain = (pack->coulomb_cur - pack->coulomb_max) / (coulomb / (elapsed_real_ms / 1000.0));
    } else {
      remain = pack->coulomb_cur / (coulomb / (elapsed_real_ms / 1000.0));
    }
    int remain_min = remain/60.0;
    int remain_sec = ((int)remain) % 60;
    printf("# ~~ A: %.2f C: %.2f (%.2f/%.2f) ~~ elapsed: %.2f ~~ remain: %02d:%02d\n", pack->ampere, coulomb, pack->coulomb_cur, pack->coulomb_max, elapsed_real_ms, remain_min, remain_sec);
  }

  if (elapsed_real_ms > 0) {
    // count coulombs
    if (pack->undervolt) {
      // if we've hit the low voltage end and coulomb_cur > 0,
      // we've overestimated the capacity by coulomb_cur.
      if (pack->coulomb_cur >= 0) {
        pack->coulomb_zero = pack->coulomb_cur;
      }
    } else {
      pack->coulomb_cur -= coulomb;
    }

    // if pack is at rest, snap to voltage based estimation
    // TODO this can be improved with a smoother curve formula
    float volt = pack->volt;
    bool gauge_very_low = (pack->coulomb_cur < (pack->coulomb_max * 0.1));
    bool gauge_not_full = (pack->coulomb_cur <= (pack->coulomb_max * 0.95));
    bool pack_discharging = (pack->ampere >= 0.1);
    bool pack_at_rest = (pack->ampere > -0.05 && pack->ampere < 0.05);
    float cells = 4;
    float low_start = cells * 2.5;
    float mid_start = cells * 3.1;
    float high_start = cells * 3.3;
    float low_range = mid_start - low_start;
    float mid_range = high_start - mid_start;
    bool voltage_low = (volt >= low_start && volt < mid_start);
    bool voltage_mid = (volt >= mid_start && volt < high_start);
    float gauge_range_low = 0.1; // 10%
    float gauge_range_mid = 0.3;

    float snap_to_coulomb = pack->coulomb_cur;
    if (gauge_not_full && pack->fully_charged) {
      // snap to 100% at the top
      snap_to_coulomb = pack->coulomb_max;
    } else if (gauge_very_low && voltage_mid) {
      // snap in this area only if the gauge has been reset/is very off,
      // as this part of the discharge curve is very flat
      snap_to_coulomb = pack->coulomb_max * (gauge_range_low + gauge_range_mid * ((volt - mid_start) / mid_range));
    } else if (voltage_low) {
      // always snap at the bottom
      snap_to_coulomb = pack->coulomb_max * (gauge_range_low * ((volt - low_start) / low_range));
    }

    if (pack_at_rest) {
      // snap to voltage after 5+ seconds at rest
      if (pack->ms_at_rest >= 5000 && snap_to_coulomb != pack->coulomb_cur) {
        pack->coulomb_cur = snap_to_coulomb;
      }
      pack->ms_at_rest += elapsed_real_ms;
      // track max 90 days
      if (pack->ms_at_rest > 3600.0 * 1000 * 24 * 90) {
	pack->ms_at_rest = 0;
      }
    } else {
      pack->ms_at_rest = 0;
      if (pack_discharging) {
	if (gauge_very_low || voltage_low) {
          pack->coulomb_cur = snap_to_coulomb;
        }
      }
    }

    // clip at 100%
    if (pack->coulomb_cur > pack->coulomb_max) {
      pack->coulomb_cur = pack->coulomb_max;
    }

    // clip at 0%
    if (pack->coulomb_cur < 0) {
      // there's more in the pack than design capacity.
      // TODO: handle this in a more sophisticated revision.
      pack->coulomb_cur = 0;
    }

    // convert to percentage
    pack->gauge_percent = (pack->coulomb_cur / pack->coulomb_max) * 100.0;
  }

  // --------------------------------------------

  //printf("[pack %d] mon_read_subcommand...\n", pack->id);

  uint8_t manufacturing_status = 0;
  mon_read_subcommand(i2c, 0x57, &manufacturing_status, 1);
  if (!(manufacturing_status & (1<<4))) {
    // FETs not enabled, setup the chip
    battery_pack_setup(i2c);
  }

  uint8_t control_status = bq76922_read_byte(i2c, 0x00);
  uint8_t safety_alert_a = bq76922_read_byte(i2c, 0x02);
  uint8_t safety_status_a = bq76922_read_byte(i2c, 0x03);
  uint8_t safety_alert_b = bq76922_read_byte(i2c, 0x04);
  uint8_t safety_status_b = bq76922_read_byte(i2c, 0x05);
  uint8_t safety_alert_c = bq76922_read_byte(i2c, 0x06);
  uint8_t safety_status_c = bq76922_read_byte(i2c, 0x07);
  //uint8_t alarm_status = bq76922_read_byte(i2c, 0x62);
  uint16_t battery_status = bq76922_read_u16(i2c, 0x12);
  uint8_t fet_status = bq76922_read_byte(i2c, 0x7f);
  uint16_t temp_int_lo = bq76922_read_byte(i2c, 0x68);
  uint16_t temp_int_hi = bq76922_read_byte(i2c, 0x69);
  uint16_t temp_ext_lo = bq76922_read_byte(i2c, 0x70);
  uint16_t temp_ext_hi = bq76922_read_byte(i2c, 0x71);
  pack->temp_int_k = temp_int_lo|(temp_int_hi<<8);
  pack->temp_ext_k = temp_ext_lo|(temp_ext_hi<<8);

  uint16_t bal_active_cells = 0;
  uint16_t bal_status1 = 0;

  bq76922_read_mem_u16(i2c, 0x0083, &bal_active_cells);
  bq76922_read_mem_u16(i2c, 0x0085, &bal_status1);

  uint8_t undervolt_threshold = 0;
  bq76922_read_mem_u8(i2c, 0x9275, &undervolt_threshold);
  uint16_t undervolt_calib = 0;
  bq76922_read_mem_u16(i2c, 0x91d4, &undervolt_calib);
  uint8_t en_protections_a = 0;
  uint8_t en_protections_b = 0;
  uint8_t en_protections_c = 0;
  bq76922_read_mem_u8(i2c, 0x9261, &en_protections_a);
  bq76922_read_mem_u8(i2c, 0x9262, &en_protections_b);
  bq76922_read_mem_u8(i2c, 0x9263, &en_protections_c);

  if (pack->debug) {
    printf("[bq76] UV thresh: %d\n", undervolt_threshold);
    printf("[bq76] UV calib: %x\n", undervolt_calib);
    printf("[bq76] en_protections_a: %x\n", en_protections_a);
    printf("[bq76] en_protections_b: %x\n", en_protections_b);
    printf("[bq76] en_protections_c: %x\n", en_protections_c);
    printf("[bq76] c1 mV: %f\n", cell1_mv);
    printf("[bq76] c2 mV: %f\n", cell2_mv);
    //printf("[bq76] c3 mV: %f\n", cell3_mv);
    printf("[bq76] c4 mV: %f\n", cell4_mv);
    printf("[bq76] c5 mV: %f\n", cell5_mv);
    printf("[bq76] stack V: %f\n", pack->volt); // FIXME ???
    printf("[bq76] pack V: %f\n", pack_mv/100.0);
    printf("[bq76] ld V: %f\n", ld_mv/100.0);
    printf("[bq76] cc2 A: %f\n", pack->ampere);
    printf("[bq76] control_status: %02x\n", control_status);
    printf("[bq76] manufacturing_status: %02x\n", manufacturing_status);
    printf("[bq76] `--     FET_EN: %d\n", !!(manufacturing_status & (1<<4)));
    printf("[bq76] `--      PF_EN: %d\n", !!(manufacturing_status & (1<<6)));
    /*printf("[bq76] `--   DSG_TEST: %d\n", !!(manufacturing_status & (1<<2)));
      printf("[bq76] `--   CHG_TEST: %d\n", !!(manufacturing_status & (1<<1)));
      printf("[bq76] `--  PCHG_TEST: %d\n", !!(manufacturing_status & (1<<0)));
      printf("[bq76] `--  PDSG_TEST: %d\n", !!(manufacturing_status & (1<<5)));*/
    printf("[bq76] battery_status: %04x\n", battery_status);
    printf("[bq76] `--  SLEEP: %d\n", !!(battery_status & (1<<15)));
    //printf("[bq76] `-- SD_CMD: %d\n", !!(battery_status & (1<<13)));
    //printf("[bq76] `--     PF: %d\n", !!(battery_status & (1<<12)));
    //printf("[bq76] `--     SS: %d\n", !!(battery_status & (1<<11)));
    /*printf("[bq76] `--   FUSE: %d\n", !!(battery_status & (1<<10)));
      printf("[bq76] `--   SEC1: %d\n", !!(battery_status & (1<<9)));
      printf("[bq76] `--   SEC0: %d\n", !!(battery_status & (1<<8)));
      printf("[bq76] `--   OTPB: %d\n", !!(battery_status & (1<<7)));
      printf("[bq76] `--   OTPW: %d\n", !!(battery_status & (1<<6)));
      printf("[bq76] `-- COWCHK: %d\n", !!(battery_status & (1<<5)));*/
    printf("[bq76] `--     WD: %d\n", !!(battery_status & (1<<4)));
    printf("[bq76] `--    POR: %d\n", !!(battery_status & (1<<3)));
    printf("[bq76] `-- SLEEPE: %d\n", !!(battery_status & (1<<2)));
    printf("[bq76] `-- PCHG_M: %d\n", !!(battery_status & (1<<1)));
    //printf("[bq76] `-- CFGUPD: %d\n", !!(battery_status & (1<<0)));
    printf("[bq76] fet_status: %02x\n", fet_status);
    printf("[bq76] `-- ALRT: %d\n", !!(fet_status & (1<<6)));
    printf("[bq76] `-- PDSG: %d\n", !!(fet_status & (1<<3)));
    printf("[bq76] `--  DSG: %d\n", !!(fet_status & (1<<2)));
    printf("[bq76] `-- PCHG: %d\n", !!(fet_status & (1<<1)));
    printf("[bq76] `--  CHG: %d\n", !!(fet_status & (1<<0)));
    printf("[bq76] safety_alert_a:  %02x\n", safety_alert_a);
    printf("[bq76] safety_alert_b:  %02x\n", safety_alert_b);
    printf("[bq76] safety_alert_c:  %02x\n", safety_alert_c);
    printf("[bq76] safety_status_a: %02x\n", safety_status_a);
    printf("[bq76] safety_status_b: %02x\n", safety_status_b);
    printf("[bq76] safety_status_c: %02x\n", safety_status_c);

    // TODO double check calculation
    printf("[bq76] temp_int: %f C\n", (pack->temp_int_k-273.15)/100.0);
    printf("[bq76] temp_ext: %f C\n", (pack->temp_ext_k-273.15)/100.0);

    printf("[bq76] bal_status1: %d sec\n", bal_status1);
    printf("[bq76] bal_active_cells: %016b\n", bal_active_cells);
  }

  pack->bal_active_cells = bal_active_cells;

  // balance all cells above threshold
  bq76922_write_mem_u16(i2c, 0x0084, MV_BALANCE_ABOVE);
  /*if (cell4_mv > 3400) {
    bq76922_write_mem_u16(i2c, 0x0083, 8);
    } else if (cell4_mv <= 3300) {
    bq76922_write_mem_u16(i2c, 0x0083, 0);
    }*/

  if (pack->debug) {
    printf("\n[PACK %d] ===================================\n", pack->id);
    printf("cells: %.2fV %.2fV %.2fV %.2fV\n",
	   pack->cells_v[0],
	   pack->cells_v[1],
	   pack->cells_v[2],
	   pack->cells_v[3]);
    printf("current: %.2fA voltage: %.2fV\n", pack->ampere, pack->volt);
    printf("balancing: %016b\n", pack->bal_active_cells);
    printf("coulomb_cur/max: %.2f / %.2f\n", pack->coulomb_cur, pack->coulomb_max);
    printf("gauge_percent: %.2f\n", pack->gauge_percent);
    printf("fully_charged: %d\n", pack->fully_charged);
    printf("============================================\n\n");
  }

  return 1;
}

void monitor_config_update(i2c_inst_t* i2c) {
  // reset first
  bq76922_write_byte(i2c, 0x3e, 0x12);
  bq76922_write_byte(i2c, 0x3f, 0x00);
  busy_wait_us(100*1000);
  // enter config update mode
  bq76922_write_byte(i2c, 0x3e, 0x90);
  bq76922_write_byte(i2c, 0x3f, 0x00);

  uint16_t battery_status = 0;
  int cfgupd = 0;
  for (int i=0; i<10; i++) {
    battery_status = bq76922_read_u16(i2c, 0x12);
    cfgupd = !!(battery_status & (1<<0));
    //printf("# [bq76] `-- CFGUPD (expect 1) (try %d): %d\n", i, cfgupd);
    if (cfgupd) break;
    busy_wait_us(10*1000);
  }

  if (!cfgupd) {
    printf("# [bq76] `-- failed to perform CFGUPD!\n");
    return;
  }

  // 4 cells, one missing in the middle
  uint8_t vcell_mode = 16 | 8 | 0 | 2 | 1;
  bq76922_write_mem_u8(i2c, 0x9304, vcell_mode);

  // CC gain (Rsense resistor config)
  // Rsense = 15mOhms
  float ccgain = 7.5684 / 15.0;
  // manual says 7.4768 in another place
  uint8_t* ccgain_bytes = (uint8_t*)&ccgain;
  bq76922_write_mem_u8(i2c, 0x91a8, ccgain_bytes[0]);
  bq76922_write_mem_u8(i2c, 0x91a9, ccgain_bytes[1]);
  bq76922_write_mem_u8(i2c, 0x91aa, ccgain_bytes[2]);
  bq76922_write_mem_u8(i2c, 0x91ab, ccgain_bytes[3]);

  float cap_gain = ccgain * 298261.6178;
  uint8_t* cap_bytes = (uint8_t*)&cap_gain;
  bq76922_write_mem_u8(i2c, 0x91ac, cap_bytes[0]);
  bq76922_write_mem_u8(i2c, 0x91ad, cap_bytes[1]);
  bq76922_write_mem_u8(i2c, 0x91ae, cap_bytes[2]);
  bq76922_write_mem_u8(i2c, 0x91af, cap_bytes[3]);

  // OCC threshold based on sense resistor
  bq76922_write_mem_u8(i2c, 0x9280, 2*15);
  // OCD1 threshold based on sense resistor
  bq76922_write_mem_u8(i2c, 0x9282, 4*15);
  // OCD2 threshold based on sense resistor
  bq76922_write_mem_u8(i2c, 0x9284, 3*15);
  // SCD threshold based on sense resistor
  bq76922_write_mem_u8(i2c, 0x9286, 15); // 150mV (max is 15 = 500mV)

  // turn off TS1 thermistor (FIXME)
  bq76922_write_mem_u8(i2c, 0x92fd, 0);

  // TODO: read back and check these values

  // set CUV (undervolt threshold)
  //bq76922_write_mem_u8(i2c, 0x91d4, 2400/50.6);
  // overvoltage threshold
  //bq76922_write_mem_u8(i2c, 0x91d6, 4200/50.6);

  // set CUV (undervolt threshold)
  //bq76922_write_mem_u16(i2c, 0x91d4, 0xffff);
  // overvoltage threshold
  //bq76922_write_mem_u16(i2c, 0x91d6, 0xffff);

  // defaults:
  // 7: Short Circuit in Discharge Protection
  // 3: Cell Overvoltage Protection
  // not enabled by default:
  // 2: Cell Undervoltage Protection
  bq76922_write_mem_u8(i2c, 0x9261, (1<<7) | (1<<3) | (1<<2));

  // power config
  // default: 0x2982
  //  13 DPSLP_OT
  //  12 SHUT_TS2
  //  11 DPSLP_PD
  //  10 DPSLP_LDO
  //  9 DPSLP_LFO
  //  8 SLEEP
  //  7 OTSD
  //  6 FASTADC
  //  5–4 CB_LOOP_SLOW_1-CB_LOOP_SLOW_0
  //  3–2 LOOP_SLOW_1-LOOP_SLOW_0
  //  1-0 WK_SPD_1–WK_SPD_0
  bq76922_write_mem_u16(i2c, 0x9234,
                        (1<<13) |
                        (0<<12) | // SHUTDOWN mode replaced by low-power state waiting for rising edge on LD pin
                        (1<<11) |
                        (0<<10) |
                        (0<<9) |
                        (1<<8) |
                        (1<<7) |
                        (0<<6) |
                        (0<<5) |
                        (0<<4) |
                        (0<<3) |
                        (0<<2) |
                        (1<<1) |
                        (0<<0));

  // shutdown cell voltage, unit mV
  bq76922_write_mem_i16(i2c, 0x923f, 2400);
  // shutdown stack voltage, unit 10mV
  bq76922_write_mem_i16(i2c, 0x9241, (2400*4)/10);

  // configure protections (A)
  // 7 = SCD
  // 6 = OCD2
  // 5 = OCD1
  // 4 = OCC
  // 3 = COV
  // 2 = CUV
  bq76922_write_mem_u8(i2c, 0x925f, (1<<3)|(1<<2));

  // TODO: protections B, internal overtemp etc
  // TODO: Shutdown Stack Voltage

  // disable all FET protections :0
  /*bq76922_write_mem_u8(i2c, 0x9265, 0);
  bq76922_write_mem_u8(i2c, 0x9266, 0);
  bq76922_write_mem_u8(i2c, 0x9267, 0);
  bq76922_write_mem_u8(i2c, 0x9269, 0);
  bq76922_write_mem_u8(i2c, 0x926a, 0);
  bq76922_write_mem_u8(i2c, 0x926b, 0);*/

  // FET options
  // 0 = SFET (series fet mode)
  // 1 = SLEEPCHG (chg fet may be enabled in sleep mode)
  // 2 = HOST_FET_EN (host fet control is allowed)
  // 3 = FET_CTRL_EN (fets are controlled by the device)
  // 4 = PDSG_EN (pdsg fet is enabled)
  // 5 = FET_INIT_OFF (default state allows fets to be on)
  bq76922_write_mem_u8(i2c, 0x9308, (1<<4)|(1<<3)|(1<<2)|(1<<1)|(1<<0));

  // enable normal FET control in Mfg Status Init
  // FIXME doesn't seem to work
  // 4 = FET_EN (normal fet control is enabled, test mode disabled)
  // 6 = PF_EN (permanent failure checks are enabled)
  // 7 = OPTW_EN (OTP writable, we don't enable this)
  bq76922_write_mem_u16(i2c, 0x9343, (0<<6)|(1<<4));

  // enable balancing
  // Cell Balancing Config / Balancing Configuration
  bq76922_write_mem_u8(i2c, 0x9335, (0<<4)|(1<<3)|(1<<2)|(1<<1)|(1<<0));

  // cell balance interval in seconds
  bq76922_write_mem_u8(i2c, 0x9339, 10);
  // cell balance max cells
  bq76922_write_mem_u8(i2c, 0x933a, 2);
  // cell balance min cell v (charge)
  bq76922_write_mem_i16(i2c, 0x933b, 3300);
  // cell balance min cell v (relax)
  bq76922_write_mem_i16(i2c, 0x933f, 3300);

  // exit config update mode
  bq76922_write_byte(i2c, 0x3e, 0x92);
  bq76922_write_byte(i2c, 0x3f, 0x00);

  battery_status = bq76922_read_u16(i2c, 0x12);
  //printf("[bq76] `-- CFGUPD (expect 0): %d\n", !!(battery_status & (1<<0)));
}

void battery_pack_setup(i2c_inst_t* i2c) {
  monitor_config_update(i2c);
  mon_sleep_off(i2c);
  mon_toggle_fet_en(i2c);
}

void battery_packs_enter_ship_mode(struct machine* mach) {
  mon_sleep_on(mach->packs[0].i2c);
  mon_sleep_on(mach->packs[1].i2c);
  mon_discharge_fets_off(mach->packs[0].i2c);
  mon_discharge_fets_off(mach->packs[1].i2c);
  // at this point we should lose power (except if connected to AC)
}
