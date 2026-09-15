/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2023-2026 MNT Research GmbH

  MNT Reform Next Battery Pack
 */

#include <stdio.h>
#include "machine_next.h"
#include "next_charger.h"
#include "bq25792.h"

void charger_task(struct machine* mach) {
  // see https://www.ti.com/lit/ds/symlink/bq25792.pdf

  //printf("[charger_configure] ~~~~~~~~~~~~~~~~~~\n");

  // TODO:
  // - [x] REG00_Minimal_System_Voltage
  // - [x] REG01_Charge_Voltage_Limit range: 3000mV - 18800mV. 16 bit reg
  //   - should be set to around 14.4-15.6? (4x3.9)
  //   - bit step 10mV
  // - [ ] REG05_Input_Voltage_Limit ?
  // - [ ] REG08_Precharge_Control
  // - [ ] REG09_Termination_Control
  // - [ ] REG0A_Re-charge_Control
  // - [ ] REG0E_Timer_Control
  // - [x] REG0F_Charger_Control_0 (some interesting stuff here like ICO)
  // - [x] REG14_Charger_Control_5 -> EN_IBAT (bit 5)

  charger_init(mach);
}

int charger_get_total_capacity_mah(struct machine* mach) {
  return mach->active_packs * 4 * mach->cell_max_mah;
}

void charger_init(struct machine* mach) {
  // reset all registers

  // TODO enforce max limits here too
  if (!mach->charger_charge_current_ma) {
    mach->charger_charge_current_ma = 100;
  }
  if (!mach->charger_input_current_ma) {
    mach->charger_input_current_ma = 500;
  }

  // count active packs
  int act_packs = 0;
  if (mach->packs[0].active) act_packs++;
  if (mach->packs[1].active) act_packs++;
  mach->active_packs = act_packs;
  if (mach->cell_max_mah < 1000 || mach->cell_max_mah >= 5000) {
    mach->cell_max_mah = 2000;
  }

  //printf("[charger_init] ~~~~~~~~~~~~~~~~~~\n");

  // turn off charging until PD allows it
  // bit 7 = EN_AUTO_IBATDIS
  //bq25792_write_byte(0x0f, 0b10000000);

  // see https://www.ti.com/lit/ds/symlink/bq25792.pdf
  // VREG = charge voltage
  bq25792_write_word(0x01, 14600 / 10); // charge voltage (conservative), VREG

  //bq25792_write_byte(0x00, (12000 - 2500) / 250); // 10.0V vsysmin, 250mV step, 2500mV offset
  bq25792_write_byte(0x00, 0b00); // 2.5V vsysmin

  bq25792_write_word(0x03, mach->charger_charge_current_ma / 10); // charge current
  bq25792_write_word(0x06, mach->charger_input_current_ma / 10); // input current, defaults to 3A @ reset (60W)

  // ADC control: 0x2e (default: 0x30)
  bq25792_write_byte(0x2e, (1<<7) | (0b00 << 4) ); // enable ADC at 15 bit (7=ADC_EN, 5:4=ADC_SAMPLE)

  // lets try one shot
  //bq25792_write_byte(0x2e, (1<<7) | (1<<6) | (0b10 << 4) ); // enable ADC at 15 bit (7=ADC_EN, 5:4=ADC_SAMPLE)

  // default IOTG setting (3000mA)
  //bq25792_write_byte(0x0d, 0b01001011);

  // charger_control_2
  // bit6: AUTO_INDET_EN (default on, D+/D- detection)
  bq25792_write_byte(0x11, 0b00000000);

  // charger_control_3
  // bit0: disable "out of audio" in forward mode
  // bit4: disable PFM in forward mode
  // bit7: disable ACDRV
  // bit2: disable batfet LDO
  bq25792_write_byte(0x12, 0b10000100);

  // charger_control_4
  // disable ACDRV
  // bit 2: disable OTG uvp
  // bit 0: enable IBUS_OCP
  // bit 5: PWM @ 750kHz
  bq25792_write_byte(0x13, 0b00100001);

  // charger_control_5
  // disable EXTILIM
  // bit 2: enable IINDPM
  // bit 3+4: OTG regulation: 5A
  // bit 5: enable IBAT discharge current sensing
  // disable BAT OCP
  //bq25792_write_byte(0x14, 0b00111100);
  bq25792_write_byte(0x14, 0b00110100);

  // FIXME: without IINDPM, we immediately get BAT_OVP+BUS_OVP with no battery

  // VOTG test
  //bq25792_write_byte(0x0b, 0);
  //bq25792_write_byte(0x0c, 0);

  // dpdm driver test
  //bq25792_write_byte(0x47, 0);
}

// current in mA
void charger_set_charge_current(struct machine *mach, int ma) {
  if (ma < 0) ma = 0;
  if (ma > 2000) ma = 2000;
  printf("# [charger] setting charge current limit %d mA\n", ma);
  mach->charger_charge_current_ma = ma;
  bq25792_write_word(0x03, mach->charger_charge_current_ma / 10);
}

// current in mA
void charger_set_input_current(struct machine *mach, int ma) {
  if (ma < 1000) ma = 1000;
  if (ma > 4000) ma = 4000;
  printf("# [charger] setting input current limit %d mA\n", ma);
  mach->charger_input_current_ma = ma;
  charger_set_charge_current(mach, ma/2); // FIXME
  bq25792_write_word(0x06, mach->charger_input_current_ma / 10);
}

void charger_shutdown() {
  // charger_control_0
  bq25792_write_byte(0x0f, 0b00000000);
  // ADC control: 0x2e (default: 0x30)
  bq25792_write_byte(0x2e, (0<<7) | (0b00 << 4)); // (7=ADC_EN, 5:4=ADC_SAMPLE)
}

// returns VBUS measurement
// TODO: use the whole machine struct
// don't return the voltage
int charger_status(struct machine *mach) {
  // FIXME: this function itself shouldn't printf
  // as we're in an IRQ callback

  struct battery_pack *packs = mach->packs;
  bool pack_debug = mach->print_pack_info;

  if (pack_debug) {
    printf("\n---------------------------\n");
  }

  // charger_control_1
  // bit3: WD_RST
  // bit2-0: watchdog timeout
  bq25792_write_byte(0x10, 0b00001000);

  // charger_control_0
  // bit7: EN_AUTO_IBATDIS
  // bit6: FORCE_IBATDIS
  // bit5: EN_CHG
  // bit4: EN_ICO
  // bit3: FORCE_ICO
  // bit2: EN_HIZ
  // bit1: EN_TERM

  bool charging_allowed = false;
  if (packs[0].active && packs[1].active) {
    if (!packs[0].overvolt && !packs[1].overvolt && !(packs[0].fully_charged && packs[1].fully_charged)) {
      charging_allowed = true;
    }
  } else if (packs[0].active && !packs[0].overvolt && !packs[0].fully_charged) {
    charging_allowed = true;
  } else if (packs[1].active && !packs[1].overvolt && !packs[1].fully_charged) {
    charging_allowed = true;
  }

  if (!packs[0].active && !packs[1].active) {
    // disable charging
    // FIXME: we can't get discharged packs online like this
    if (pack_debug) {
      printf("# [bq25] disable/trickle charging (no packs connected).\n");
    }
    bq25792_write_byte(0x0f, 0b10000000);
  } else if (!charging_allowed) {
    // disable charging
    if (pack_debug) {
      printf("# [bq25] disable charging (overvoltage/fully charged).\n");
    }
    bq25792_write_byte(0x0f, 0b10000000);
  } else {
    // enable charging
    if (pack_debug) {
      printf("# [bq25] enable charging.\n");
    }
    bq25792_write_byte(0x0f, 0b10100000);
  }

  uint8_t charger_status_0 = bq25792_read_byte(0x1b);
  uint8_t charger_status_1 = bq25792_read_byte(0x1c);
  uint8_t charger_status_2 = bq25792_read_byte(0x1d);
  uint8_t charger_status_3 = bq25792_read_byte(0x1e);
  uint8_t charger_status_4 = bq25792_read_byte(0x1f);
  uint8_t fault_status_0 = bq25792_read_byte(0x20);
  uint8_t fault_status_1 = bq25792_read_byte(0x21);
  //uint8_t recharge_ctl = bq25792_read_byte(0x0a);
  //uint8_t cell_count = (recharge_ctl >> 6) && 0b11;
  uint8_t fault_flag_0 = bq25792_read_byte(0x26);
  uint8_t fault_flag_1 = bq25792_read_byte(0x27);

  int16_t ibus_adc = bq25792_read_word_signed(0x31); // 1mA resolution
  int16_t ibat_adc = bq25792_read_word_signed(0x33); // 1mA resolution
  int16_t ilim = bq25792_read_word_signed(0x19)*10; // 10mA resolution
  uint16_t vbus_adc = bq25792_read_word(0x35); // 1mV resolution
  uint16_t vac1_adc = bq25792_read_word(0x37); // 1mV resolution
  //uint16_t vac2_adc = bq25792_read_word(0x39); // 1mV resolution
  uint16_t vbat_adc = bq25792_read_word(0x3b); // 1mV resolution
  // FIXME unused
  uint16_t vsys_adc = bq25792_read_word(0x3d); // 1mV resolution
  float tdie_adc = (float)bq25792_read_word_signed(0x41) * 0.5; // 0.5 celsius resolution

  // FIXME not here
  //if (!packs[0].active && !packs[1].active) {
    //report_volts = vbus_adc/100.0;
    //report_current = ibus_adc/1000.0;
  //}

  if (pack_debug) {
    printf("# [bq25] charger_status_0: %08b\n", charger_status_0);
    if (charger_status_0 & 0b1) printf("[bq25] `-- VBUS present\n");
    if (charger_status_0 & 0b10) printf("[bq25] `-- VAC1 present\n");
    if (charger_status_0 & 0b100) printf("[bq25] `-- VAC2 present\n");
    if (charger_status_0 & 0b1000) printf("[bq25] `-- Power good\n");
    if (!(charger_status_0 & 0b1000)) printf("[bq25] `-- Power not good\n");
    if (charger_status_0 & 0b10000) printf("[bq25] `-- Poor source\n");
    if (charger_status_0 & 0b100000) printf("[bq25] `-- WD timer expired\n");
    if (charger_status_0 & 0b1000000) printf("[bq25] `-- VINDPM/VOTG\n");
    if (charger_status_0 & 0b10000000) printf("[bq25] `-- IINDPM/IOTG\n");
    printf("[bq25] charger_status_1: %08b\n", charger_status_1);
    if ((charger_status_1 & 0b11100000)>>5 == 0) printf("[bq25] `-- not charging\n");
    if ((charger_status_1 & 0b11100000)>>5 == 1) printf("[bq25] `-- trickle charge\n");
    if ((charger_status_1 & 0b11100000)>>5 == 2) printf("[bq25] `-- pre-charge\n");
    if ((charger_status_1 & 0b11100000)>>5 == 3) printf("[bq25] `-- fast charge CC\n");
    if ((charger_status_1 & 0b11100000)>>5 == 4) printf("[bq25] `-- taper charge CV\n");
    if ((charger_status_1 & 0b11100000)>>5 == 5) printf("[bq25] `-- reserved\n");
    if ((charger_status_1 & 0b11100000)>>5 == 6) printf("[bq25] `-- top-off timer\n");
    if ((charger_status_1 & 0b11100000)>>5 == 7) printf("[bq25] `-- termination done\n");

    if ((charger_status_1 & 0b11110)>>1 == 0) printf("[bq25] `-- no input or bhot/bcold in otg\n");
    if ((charger_status_1 & 0b11110)>>1 == 1) printf("[bq25] `-- USB SDP\n");
    if ((charger_status_1 & 0b11110)>>1 == 2) printf("[bq25] `-- USB DCP\n");
    if ((charger_status_1 & 0b11110)>>1 == 3) printf("[bq25] `-- no input or bhot/bcold in otg\n");
    if ((charger_status_1 & 0b11110)>>1 == 4) printf("[bq25] `-- HVDCP\n");
    if ((charger_status_1 & 0b11110)>>1 == 5) printf("[bq25] `-- unknown adapter (3A)\n");
    if ((charger_status_1 & 0b11110)>>1 == 6) printf("[bq25] `-- non-standard adapter\n");
    if ((charger_status_1 & 0b11110)>>1 == 7) printf("[bq25] `-- OTG mode\n");
    if ((charger_status_1 & 0b11110)>>1 == 8) printf("[bq25] `-- not qualified adapter\n");
    if ((charger_status_1 & 0b11110)>>1 == 0xb) printf("[bq25] `-- directly powered from VBUS\n");

    printf("[bq25] charger_status_2: %08b\n", charger_status_2);
    if (charger_status_2 & 0b1) printf("[bq25] `-- VBAT present\n");
    printf("[bq25] charger_status_3: %08b\n", charger_status_3);
    if (charger_status_3 & 0b100000) printf("[bq25] `-- ADC done\n");
    if ((charger_status_3 & 0b100000) == 0) printf("[bq25] `-- ADC not done\n");
    if (charger_status_3 & 0b10000) printf("[bq25] `-- VBAT < VSYSmin\n");
    printf("[bq25] charger_status_4: %08b\n", charger_status_4);
    if (charger_status_4 & 0b10000) printf("[bq25] `-- VBAT too low for OTG\n");

    printf("[bq25] fault_status_0  : %08b\n", fault_status_0);
    if (fault_status_0 & 0b1) printf("[bq25] `-- VAC1 over-voltage\n");
    if (fault_status_0 & 0b10) printf("[bq25] `-- VAC2 over-voltage\n");
    if (fault_status_0 & 0b100) printf("[bq25] `-- Converter over-current\n");
    if (fault_status_0 & 0b1000) printf("[bq25] `-- IBAT over-current\n");
    if (fault_status_0 & 0b10000) printf("[bq25] `-- IBUS over-current\n");
    if (fault_status_0 & 0b100000) printf("[bq25] `-- VBAT over-voltage\n");
    if (fault_status_0 & 0b1000000) printf("[bq25] `-- VBUS over-voltage\n");
    if (fault_status_0 & 0b10000000) printf("[bq25] `-- IBAT regulation\n");

    printf("[bq25] fault_status_1  : %08b\n", fault_status_1);
    if (fault_status_1 & 0b100) printf("[bq25] `-- Thermal shutdown\n");
    if (fault_status_1 & 0b10000) printf("[bq25] `-- OTG under-voltage\n");
    if (fault_status_1 & 0b100000) printf("[bq25] `-- OTG over-voltage\n");
    if (fault_status_1 & 0b1000000) printf("[bq25] `-- VSYS over-voltage\n");
    if (fault_status_1 & 0b10000000) printf("[bq25] `-- VSYS short circuit\n");

    printf("[bq25] fault_flag_0  : %08b\n", fault_flag_0);
    printf("[bq25] fault_flag_1  : %08b\n", fault_flag_1);
  }

  uint16_t vreg = bq25792_read_word(0x01) * 10; // 10mV resolution
  uint16_t ichg = bq25792_read_word(0x03) * 10; // 10mA resolution

  mach->charger_battery_ma = ibat_adc;
  mach->charger_battery_mv = vbat_adc;
  mach->charger_charge_ma = ichg;
  mach->charger_charge_mv = vreg;
  mach->charger_temperature_c = tdie_adc;
  mach->charger_input_mv = vac1_adc;
  mach->charger_input_ma = ibus_adc;
  mach->charger_input_limit_ma = ilim;
  mach->charger_sys_mv = vsys_adc;

  //printf("[bq25] charger_status_2: %08b\n", charger_status_2);

  if (pack_debug) {
    printf("[bq25] ICHG: %d mA\n", ichg);
    printf("[bq25] VREG: %d mV\n", vreg);

    printf("[bq25] vbus: %d mV\n", vbus_adc);
    printf("[bq25] vac1: %d mV\n", vac1_adc);
    printf("[bq25] vbat: %d mV\n", vbat_adc);
    printf("[bq25] ibus: %d mA\n", ibus_adc);
    printf("[bq25] ibat: %d mA\n", ibat_adc);
    printf("[bq25] ilim: %d mA\n", ilim);
    printf("[bq25] tdie: %f C\n",  tdie_adc);
    printf("---------------------------\n");
  }

  return vbus_adc;
}
