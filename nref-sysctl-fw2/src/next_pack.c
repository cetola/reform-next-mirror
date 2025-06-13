#include <next_pack.h>

// BQ76922 monitor on battery boards
#define BQ76922_ADDR 0x08
#define I2C_TIMEOUT (1000*500)

#include <bq76922.h>

int pack_configure(struct BatteryPack* pack, float ms_elapsed) {
  // subcommand: lo to 0x3e, hi to 0x3f
  // read 0x3e, 0x3f. if == 0xff, busy
  //                  if == written subcommand, done
  // read 0x61 response length
  // read 0x40... response length (max. up to 0x60)
  // later: check checksum (at 0x60). checksum includes 0x3e, 0x3f
  // don't read checksum and length at the same time (auto-increment stuff)

  // later, we can write defaults to OTP memory

  i2c_inst_t* i2c = pack->i2c;

  int detected = bq76922_detect(i2c);

  if (detected != 1) {
    printf("[pack %d] not detected: %d\n", pack->id, detected);
    // pack not connected
    pack->cells_v[0] = 0;
    pack->cells_v[1] = 0;
    pack->cells_v[2] = 0;
    pack->cells_v[3] = 0;
    pack->undervolt = 0;
    pack->overvolt = 0;
    pack->active = false;
    // TODO reset coulomb counter?
    return 0;
  } else {
    pack->active = true;
  }

  uint16_t cell1_mv_lo = bq76922_read_byte(i2c, 0x14);
  uint16_t cell1_mv_hi = bq76922_read_byte(i2c, 0x15);
  uint16_t cell2_mv_lo = bq76922_read_byte(i2c, 0x16);
  uint16_t cell2_mv_hi = bq76922_read_byte(i2c, 0x17);
  uint16_t cell3_mv_lo = bq76922_read_byte(i2c, 0x18);
  uint16_t cell3_mv_hi = bq76922_read_byte(i2c, 0x19);
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
  float cell3_mv = cell3_mv_lo|(cell3_mv_hi<<8);
  float cell4_mv = cell4_mv_lo|(cell4_mv_hi<<8);
  float cell5_mv = cell5_mv_lo|(cell5_mv_hi<<8);
  float stack_mv = (int16_t)(stack_userv_lo|(stack_userv_hi<<8));
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
    if (!pack->fully_charged) {
      // arrived at top end. if we never fully discharged,
      // we don't know the actual capacity. if the capacity
      // seems unrealistically low, reset to default capacity
      if (pack->coulomb_max < MAX_CAPACITY * 0.4) {
        // FIXME experiment with these numbers
        pack->coulomb_cur = MAX_CAPACITY * 0.9;
        pack->coulomb_max = MAX_CAPACITY * 0.9;
      }
    }
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
    printf("[bq76:%d] undervoltage, turning discharge off.\n", pack->id);
    mon_discharge_fets_off(i2c);
  } else if (pack->overvolt) {
    printf("[bq76:%d] overvoltage, turning charge off.\n", pack->id);
    mon_charge_fets_off(i2c);
  } else {
    mon_all_fets_on(i2c);
  }

  pack->volt = stack_mv/100.0; // default unit is centivolts
  pack->ampere = cc2_ma/1000.0; // default unit is mA

  // coulomb counting (gauge) -------------------

  float coulomb = pack->ampere * (ms_elapsed / 1000.0);
  //printf("~~ coloumb: %.2f ~~ elapsed: %f ms\n", coulomb, ms_elapsed);

  if (ms_elapsed > 0) {
    if (pack->undervolt) {
      // if we've hit the low voltage end and coulomb_cur > 0,
      // we've overestimated the capacity by coulomb_cur.
      if (pack->coulomb_cur > 0) {
        pack->coulomb_max -= pack->coulomb_cur;
      }
      pack->coulomb_cur = 0;
    } else if (pack->overvolt) {
      // FIXME how to count balancing current?
      // - we could stop counting during balancing.
      pack->coulomb_max = pack->coulomb_cur;
    } else {
      pack->coulomb_cur -= coulomb;
    }

    if (pack->coulomb_max > MAX_CAPACITY) {
      pack->coulomb_max = MAX_CAPACITY;
    }

    if (pack->coulomb_cur > pack->coulomb_max) pack->coulomb_max = pack->coulomb_cur;
    if (pack->coulomb_cur < 0) {
      // there's more in the pack than expected, add to _max
      pack->coulomb_max -= pack->coulomb_cur;
      pack->coulomb_cur = 0;
    }

    if (pack->coulomb_max <= 0) {
      pack->gauge_percent = 0;
    } else {
      pack->gauge_percent = (pack->coulomb_cur / pack->coulomb_max) * 100.0;
    }
  }

  // --------------------------------------------

  uint8_t manufacturing_status = 0;
  monitor_read_subcommand(i2c, 0x57, &manufacturing_status, 1);
  if (!(manufacturing_status & (1<<4))) {
    // FETs not enabled, setup the chip
    monitor_setup(i2c);
  }

  uint8_t control_status = bq76922_read_byte(i2c, 0x00);
  uint8_t safety_alert_a = bq76922_read_byte(i2c, 0x02);
  uint8_t safety_status_a = bq76922_read_byte(i2c, 0x03);
  uint8_t safety_alert_b = bq76922_read_byte(i2c, 0x04);
  uint8_t safety_status_b = bq76922_read_byte(i2c, 0x05);
  uint8_t safety_alert_c = bq76922_read_byte(i2c, 0x06);
  uint8_t safety_status_c = bq76922_read_byte(i2c, 0x07);
  uint8_t alarm_status = bq76922_read_byte(i2c, 0x62);
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

  if (pack_debug) {
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
    /*printf("[bq76] safety_alert_a:  %02x\n", safety_alert_a);
      printf("[bq76] safety_alert_b:  %02x\n", safety_alert_b);
      printf("[bq76] safety_alert_c:  %02x\n", safety_alert_c);
      printf("[bq76] safety_status_a: %02x\n", safety_status_a);
      printf("[bq76] safety_status_b: %02x\n", safety_status_b);
      printf("[bq76] safety_status_c: %02x\n", safety_status_c);*/

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

  return 1;
}

void monitor_config_update(i2c_inst_t* i2c) {
  // enter config update mode
  bq76922_write_byte(i2c, 0x3e, 0x90);
  bq76922_write_byte(i2c, 0x3f, 0x00);

  uint16_t battery_status = 0;
  int cfgupd = 0;
  for (int i=0; i<10; i++) {
    battery_status = bq76922_read_u16(i2c, 0x12);
    cfgupd = !!(battery_status & (1<<0));
    printf("[bq76] `-- CFGUPD (expect 1) (try %d): %d\n", i, cfgupd);
    if (cfgupd) break;
    sleep_ms(10);
  }

  if (!cfgupd) {
    printf("[bq76] `-- failed to perform CFGUPD!\n");
    return;
  }

  // 4 cells, one missing in the middle
  uint8_t vcell_mode = 16 | 8 | 0 | 2 | 1;
  bq76922_write_mem_u8(i2c, 0x9304, vcell_mode);

  // TODO: read back and check these values

  // disable all FET protections :0
  /*bq76922_set_reg(0x9265, 0, 1);
    bq76922_set_reg(0x9266, 0, 1);
    bq76922_set_reg(0x9267, 0, 1);
    bq76922_set_reg(0x9269, 0, 1);
    bq76922_set_reg(0x926a, 0, 1);
    bq76922_set_reg(0x926b, 0, 1);*/

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
  bq76922_write_mem_u16(i2c, 0x9343, (1<<6)|(1<<4));

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
  printf("[bq76] `-- CFGUPD (expect 0): %d\n", !!(battery_status & (1<<0)));
}

void monitor_setup(i2c_inst_t* i2c) {
  int id = 0;
  if (i2c == i2c1) id = 1;

  printf("[bq76:%d] monitor_setup begin\n", id);

  monitor_config_update(i2c);
  mon_sleep_off(i2c);
  mon_toggle_fet_en(i2c);

  printf("[bq76:%d] monitor_setup done\n", id);
}
