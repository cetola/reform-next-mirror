#include <stdint.h>
#include <stdio.h>
#include "hardware/i2c.h"
#include "next_pack.h"
#include "bq25792.h"

// BQ25792 charger
#define BQ25792_ADDR 0x6b

#define I2C_TIMEOUT (1000*500)

// FIXME
static int pack_debug = 0;

uint8_t bq25792_read_byte(uint8_t addr)
{
  uint8_t buf;
  i2c_write_blocking(i2c0, BQ25792_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25792_ADDR, &buf, 1, false);
  return buf;
}

int16_t bq25792_read_word_signed(uint8_t addr)
{
  uint8_t buf[2] = {0,0};
  i2c_write_blocking(i2c0, BQ25792_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25792_ADDR, &buf[0], 1, false);
  addr++;
  i2c_write_blocking(i2c0, BQ25792_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25792_ADDR, &buf[1], 1, false);

  int16_t lsb = buf[1];
  int16_t msb = buf[0];

  return (msb<<8) | lsb;
}

uint16_t bq25792_read_word(uint8_t addr)
{
  uint8_t buf[2] = {0,0};
  i2c_write_blocking(i2c0, BQ25792_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25792_ADDR, &buf[0], 1, false);
  addr++;
  i2c_write_blocking(i2c0, BQ25792_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25792_ADDR, &buf[1], 1, false);

  int16_t lsb = buf[1];
  int16_t msb = buf[0];

  return (msb<<8) | lsb;
}

void bq25792_write_byte(uint8_t addr, uint8_t byte)
{
  uint8_t buf[2] = {addr, byte};
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf, 2, false);
}

void bq25792_write_word(uint8_t addr, uint16_t word)
{
  uint8_t buf_msb[2] = {addr, word>>8};
  uint8_t buf_lsb[2] = {addr+1, word & 0xff};

  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_msb, 2, false);
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_lsb, 2, false);
}

void bq25792_write_word_signed(uint8_t addr, int16_t word)
{
  uint8_t buf_msb[2] = {addr, word>>8};
  uint8_t buf_lsb[2] = {addr+1, word & 0xff};

  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_msb, 2, false);
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_lsb, 2, false);
}

void charger_configure() {
  // see https://www.ti.com/lit/ds/symlink/bq25792.pdf

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
}

void charger_init()
{
  // reset all registers

  // TODO turn off charging until PD allows it

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
}

// returns VBUS measurement
int charger_status(struct BatteryPack* packs) {
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
  if ((packs[0].overvolt && packs[1].overvolt) || (packs[0].fully_charged && packs[1].fully_charged)) {
    // disable charging
    printf("# [bq25] disable charging (overvoltage/fully charged).\n");
    bq25792_write_byte(0x0f, 0b00000000);
  } else if (!packs[0].active && !packs[1].active) {
    // disable charging
    // FIXME: can we still get discharged packs online?
    printf("# [bq25] disable charging (no packs connected).\n");
    bq25792_write_byte(0x0f, 0b00000000);
  } else {
    // enable charging
    if (pack_debug) {
      printf("# [bq25] enable charging.\n");
    }
    bq25792_write_byte(0x0f, 0b00100000);
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

  int16_t ibus_adc = bq25792_read_word_signed(0x31); // 1mA resolution
  int16_t ibat_adc = bq25792_read_word_signed(0x33); // 1mA resolution
  int16_t ilim = bq25792_read_word_signed(0x19)*10; // 10mA resolution
  uint16_t vbus_adc = bq25792_read_word(0x35); // 1mV resolution
  uint16_t vac1_adc = bq25792_read_word(0x37); // 1mV resolution
  //uint16_t vac2_adc = bq25792_read_word(0x39); // 1mV resolution
  uint16_t vbat_adc = bq25792_read_word(0x3b); // 1mV resolution
  // FIXME unused
  //uint16_t vsys_adc = bq25792_read_word(0x3d); // 1mV resolution
  float tdie_adc = (float)bq25792_read_word_signed(0x41) * 0.5; // 0.5 celsius resolution

  // FIXME not here
  //if (!packs[0].active && !packs[1].active) {
    //report_volts = vbus_adc/100.0;
    //report_current = ibus_adc/1000.0;
  //}

  if (1 || pack_debug) {
    printf("[bq25] charger_status_0: %08b\n", charger_status_0);
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
    printf("[bq25] charger_status_2: %08b\n", charger_status_2);
    printf("[bq25] charger_status_3: %08b\n", charger_status_3);
    printf("[bq25] charger_status_4: %08b\n", charger_status_4);

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

  }

  uint16_t vreg = bq25792_read_word(0x01)*10; // 10mV resolution
  uint16_t ichg = bq25792_read_word(0x03)*10; // 10mA resolution

  printf("[bq25] charger_status_2: %08b\n", charger_status_2);

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

  return vbus_adc;
}
