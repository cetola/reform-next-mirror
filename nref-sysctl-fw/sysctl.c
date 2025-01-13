/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2040
  Copyright 2023-2024 MNT Research GmbH

  fusb_read/write functions based on:
  https://git.clarahobbs.com/pd-buddy/pd-buddy-firmware/src/branch/master/lib/src/fusb302b.c
*/

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "pico/sleep.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/rtc.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/vreg_and_chip_reset.h"
#include "fusb302b.h"
#include "pd.h"

#define FW_STRING1 "NREF1SYS"
#define FW_STRING2 "R1"
#define FW_STRING3 "20241212"
#define FW_REV FW_STRING1 FW_STRING2 FW_STRING3

#define ACM_ENABLED 1

#define PIN_SDA0 0
#define PIN_SCL0 1
#define PIN_SDA1 2
#define PIN_SCL1 3

#define PIN_KBD_UART_TX 4
#define PIN_KBD_UART_RX 5
#define PIN_DISP_EN 7
#define PIN_SOM_MOSI 8
#define PIN_SOM_SS0 9
#define PIN_SOM_SCK 10
#define PIN_SOM_MISO 11
#define PIN_SOM_UART_TX 12
#define PIN_SOM_UART_RX 13
#define PIN_FUSB_INT 14
#define PIN_LED_B 15
#define PIN_LED_R 16
#define PIN_LED_G 17
#define PIN_SOM_WAKE 19
#define PIN_CHRG_CE 20
#define PIN_3V3_ENABLE 24
#define PIN_5V_ENABLE 25
//#define PIN_USB_SRC_ENABLE 28
#define PIN_PWREN_LATCH 29

// FUSB302B USB-PD controller
#define FUSB_ADDR 0x22

// PCAL6416AHF GPIO extender
#define PCAL_ADDR 0x20

// BQ25756RRVR charger

#define BQ25756_ADDR 0x6b

#define I2C_TIMEOUT (1000*500)

#define UART_ID uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

#define BOOT_MAGIC_2 0xAA55F0F0
#define BOOT_MAGIC_3 0x0F0F55AA
#define BOOT_MAGIC_OFF (io_rw_32)(-1)

// The Pico boot rom uses watchdog scratch registers 0, 1, 4, 5, 6, and 7.
// That leaves 2 and 3 for our "system is on" magic.
// A _real_ power-on reset clears these registers, so if our magic is left over
// then we have either been updated while the system is on, or have run into an
// event with probability 2**-64.
bool syscon_warm_boot() {
    return (watchdog_hw->scratch[2] == BOOT_MAGIC_2 &&
            watchdog_hw->scratch[3] == BOOT_MAGIC_3);
}

void set_boot_magic() {
    watchdog_hw->scratch[2] = BOOT_MAGIC_2;
    watchdog_hw->scratch[3] = BOOT_MAGIC_3;
}

void clear_boot_magic() {
    watchdog_hw->scratch[2] = BOOT_MAGIC_OFF;
    watchdog_hw->scratch[3] = BOOT_MAGIC_OFF;
}

// battery information
// TODO: turn into a struct
// 4.8A x 3600 seconds/hour (per cell)
#define MAX_CAPACITY (4.0)*3600.0
float report_capacity_max_ampsecs =  MAX_CAPACITY;
float report_capacity_accu_ampsecs = MAX_CAPACITY;
float report_capacity_min_ampsecs = 0;
int report_capacity_percentage = 0;
float report_volts = 0;
float report_current = 0;
float report_cells_v[8] = {0,0,0,0,0,0,0,0};
bool reached_full_charge = true; // FIXME
bool som_is_powered = false;
bool print_pack_info = false;

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

uint8_t fusb_read_byte(uint8_t addr)
{
  //printf("# [pd] FUSB read byte: %02x.\n", addr);
  uint8_t buf;
  i2c_write_blocking(i2c0, FUSB_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, FUSB_ADDR, &buf, 1, false);
  return buf;
}

void fusb_read_buf(uint8_t addr, uint8_t size, uint8_t *buf)
{
  i2c_write_blocking(i2c0, FUSB_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, FUSB_ADDR, buf, size, false);
}

void fusb_write_byte(uint8_t addr, uint8_t byte)
{
  //printf("# [pd] FUSB write byte: %02x = %02x.\n", addr, byte);
  uint8_t buf[2] = {addr, byte};
  i2c_write_blocking(i2c0, FUSB_ADDR, buf, 2, false);
}

void fusb_write_buf(uint8_t addr, uint8_t size, const uint8_t *buf)
{
  uint8_t txbuf[size + 1];
  txbuf[0] = addr;
  for (int i = 0; i < size; i++) {
    txbuf[i + 1] = buf[i];
  }
  i2c_write_blocking(i2c0, FUSB_ADDR, txbuf, size + 1, false);
}

void fusb_send_message(const union pd_msg *msg)
{
  /* Token sequences for the FUSB302B */
  static uint8_t sop_seq[5] = {
    FUSB_FIFO_TX_SOP1,
    FUSB_FIFO_TX_SOP1,
    FUSB_FIFO_TX_SOP1,
    FUSB_FIFO_TX_SOP2,
    FUSB_FIFO_TX_PACKSYM
  };
  static uint8_t eop_seq[4] = {
    FUSB_FIFO_TX_JAM_CRC,
    FUSB_FIFO_TX_EOP,
    FUSB_FIFO_TX_TXOFF,
    FUSB_FIFO_TX_TXON
  };

  /* Get the length of the message: a two-octet header plus NUMOBJ four-octet
   * data objects */
  uint8_t msg_len = 2 + 4 * PD_NUMOBJ_GET(msg);

  /* Set the number of bytes to be transmitted in the packet */
  sop_seq[4] = FUSB_FIFO_TX_PACKSYM | msg_len;

  /* Write all three parts of the message to the TX FIFO */
  fusb_write_buf(FUSB_FIFOS, 5, sop_seq);
  fusb_write_buf(FUSB_FIFOS, msg_len, msg->bytes);
  fusb_write_buf(FUSB_FIFOS, 4, eop_seq);
}

uint8_t fusb_read_message(union pd_msg *msg)
{
  uint8_t garbage[4];
  uint8_t numobj;

  /* If this isn't an SOP message, return error.
   * Because of our configuration, we should be able to assume this means the
   * buffer is empty, and not try to read past a non-SOP message. */
  uint8_t rxb = fusb_read_byte(FUSB_FIFOS);
  //if (rxb!=0) printf("# [fusb] rx = 0x%02x\n", rxb);
  if ((rxb & FUSB_FIFO_RX_TOKEN_BITS)
      != FUSB_FIFO_RX_SOP) {
    return 1;
  }
  /* Read the message header into msg */
  fusb_read_buf(FUSB_FIFOS, 2, msg->bytes);
  /* Get the number of data objects */
  numobj = PD_NUMOBJ_GET(msg);
  /* If there is at least one data object, read the data objects */
  if (numobj > 0) {
    fusb_read_buf(FUSB_FIFOS, numobj * 4, msg->bytes + 2);
  }
  /* Throw the CRC32 in the garbage, since the PHY already checked it. */
  fusb_read_buf(FUSB_FIFOS, 4, garbage);

  return 0;
}

// returns voltage
int print_src_fixed_pdo(int number, uint32_t pdo) {
  int tmp;

  printf("[pd_src_fixed_pdo]\n");
  printf("number = %d\n", number);

  /* Dual-role power */
  tmp = (pdo & PD_PDO_SRC_FIXED_DUAL_ROLE_PWR) >> PD_PDO_SRC_FIXED_DUAL_ROLE_PWR_SHIFT;
  if (tmp) {
    printf("dual_role_pwr = %d\n", tmp);
  }

  /* USB Suspend Supported */
  tmp = (pdo & PD_PDO_SRC_FIXED_USB_SUSPEND) >> PD_PDO_SRC_FIXED_USB_SUSPEND_SHIFT;
  if (tmp) {
    printf("usb_suspend = %d\n", tmp);
  }

  /* Unconstrained Power */
  tmp = (pdo & PD_PDO_SRC_FIXED_UNCONSTRAINED) >> PD_PDO_SRC_FIXED_UNCONSTRAINED_SHIFT;
  if (tmp) {
    printf("unconstrained_pwr = %d\n", tmp);
  }

  /* USB Communications Capable */
  tmp = (pdo & PD_PDO_SRC_FIXED_USB_COMMS) >> PD_PDO_SRC_FIXED_USB_COMMS_SHIFT;
  if (tmp) {
    printf("usb_comms = %d\n", tmp);
  }

  /* Dual-Role Data */
  tmp = (pdo & PD_PDO_SRC_FIXED_DUAL_ROLE_DATA) >> PD_PDO_SRC_FIXED_DUAL_ROLE_DATA_SHIFT;
  if (tmp) {
    printf("dual_role_data = %d\n", tmp);
  }

  /* Unchunked Extended Messages Supported */
  tmp = (pdo & PD_PDO_SRC_FIXED_UNCHUNKED_EXT_MSG) >> PD_PDO_SRC_FIXED_UNCHUNKED_EXT_MSG_SHIFT;
  if (tmp) {
    printf("unchunked_ext_msg = %d\n", tmp);
  }

  /* Peak Current */
  tmp = (pdo & PD_PDO_SRC_FIXED_PEAK_CURRENT) >> PD_PDO_SRC_FIXED_PEAK_CURRENT_SHIFT;
  if (tmp) {
    printf("peak_i = %d\n", tmp);
  }

  /* Voltage */
  tmp = (pdo & PD_PDO_SRC_FIXED_VOLTAGE) >> PD_PDO_SRC_FIXED_VOLTAGE_SHIFT;
  printf("v = %d.%02d\n", PD_PDV_V(tmp), PD_PDV_CV(tmp));

  int voltage = (int)PD_PDV_V(tmp);

  /* Maximum Current */
  tmp = (pdo & PD_PDO_SRC_FIXED_CURRENT) >> PD_PDO_SRC_FIXED_CURRENT_SHIFT;
  printf("i_a: %d.%02d\n", PD_PDI_A(tmp), PD_PDI_CA(tmp));

  return voltage;
}

uint8_t bq25756_read_byte(uint8_t addr)
{
  uint8_t buf;
  i2c_write_blocking(i2c0, BQ25756_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25756_ADDR, &buf, 1, false);
  return buf;
}

int16_t bq25756_read_word(uint8_t addr)
{
  uint8_t buf[2] = {0,0};
  i2c_write_blocking(i2c0, BQ25756_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25756_ADDR, &buf[0], 1, false);
  addr++;
  i2c_write_blocking(i2c0, BQ25756_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25756_ADDR, &buf[1], 1, false);

  int16_t lsb = buf[0];
  int16_t msb = buf[1];

  return (msb<<8) | lsb;
}

void bq25756_write_byte(uint8_t addr, uint8_t byte)
{
  uint8_t buf[2] = {addr, byte};
  i2c_write_blocking(i2c0, BQ25756_ADDR, buf, 2, false);
}


int charger_configure() {
  // TODO
  // see https://www.ti.com/lit/an/sluaat5/sluaat5.pdf

  bq25756_write_byte(0x2b, 1<<7); // enable ADC
  bq25756_write_byte(0x2c, (0<<7)|(0<<6)|(0<<5)|(0<<4)|(1<<1)); // enable ADC channels

  uint8_t charger_control = bq25756_read_byte(0x17);
  uint8_t charger_status_1 = bq25756_read_byte(0x21);
  uint8_t charger_status_2 = bq25756_read_byte(0x22);
  uint8_t charger_status_3 = bq25756_read_byte(0x23);
  uint8_t fault_status = bq25756_read_byte(0x24);
  uint8_t charger_flag_1 = bq25756_read_byte(0x25);
  uint8_t charger_flag_2 = bq25756_read_byte(0x26);
  uint8_t pin_control = bq25756_read_byte(0x18);

  int16_t iac_adc = bq25756_read_word(0x2d);
  int16_t ibat_adc = bq25756_read_word(0x2f);
  int16_t vac_adc = bq25756_read_word(0x31);
  int16_t vbat_adc = bq25756_read_word(0x33);

  //report_current = ibat_adc/1000.0;

  // set recharge voltage: 0x17
  bq25756_write_byte(0x17, 0b00011001);
  //                               `---- ibat_load

  // set battery low voltage (71.4% x VFB_REG)
  // disable termination (bit 3)
  // disable precharge (bit 0)
  bq25756_write_byte(0x14, 0b0110);
  // disable all safety timers
  //bq25756_write_byte(0x15, 0b0);
  // disable PFM
  bq25756_write_byte(0x19, 0b00000000);

  // FIXME: disable JEITA, TS pin on lifepo4
  //bq25756_write_byte(0x1c, 0);

  // boost charge voltage by 30mV
  bq25756_write_byte(0x00, 0b11111);
  //bq25756_write_byte(0x00, 0);

  // charge current limit (step = 50mA)
  bq25756_write_byte(0x03, 1); // upper byte
  bq25756_write_byte(0x02, (2000/50)<<2); // 400mA (0x8) is the low end

  // input current limit (step = 50mA). 50W = 20V @ 2.5A
  bq25756_write_byte(0x07, 0); // upper byte
  bq25756_write_byte(0x06, (3000/50)<<2); // 400mA (0x8) is the low end
  //bq25756_write_byte(0x07, 1<<2); // upper byte
  //bq25756_write_byte(0x06, 1); // 400mA (0x8) is the low end

  // FIXME: resistors! disable ICHG, ILIM
  bq25756_write_byte(0x18, 0);

  // TODO charge current limit

  printf("\n---------------------------\n[bq25] charger_control: %02x\n", charger_control);
  printf("[bq25] charger_status_1: %02x\n", charger_status_1);
  printf("[bq25] charger_status_2: %02x\n", charger_status_2);
  printf("[bq25] charger_status_3: %02x\n", charger_status_3);
  printf("[bq25] fault_status: %02x\n", fault_status);
  if (fault_status & 0b10) printf("[bq25] `-- DRV_SUP out of range\n");
  if (fault_status & 0b100) printf("[bq25] `-- Charge safety timer expired\n");
  if (fault_status & 0b1000) printf("[bq25] `-- Thermal shutdown\n");
  if (fault_status & 0b10000) printf("[bq25] `-- Battery overvoltage\n");
  if (fault_status & 0b100000) printf("[bq25] `-- Battery overcurrent\n");
  if (fault_status & 0b1000000) printf("[bq25] `-- Input overvoltage\n");
  if (fault_status & 0b10000000) printf("[bq25] `-- Input overcurrent\n");
  printf("[bq25] charger_flag_1: %02x\n", charger_flag_1);
  printf("[bq25] charger_flag_2: %02x\n", charger_flag_2);
  printf("[bq25] pin_control: %02x\n", pin_control);

  printf("[bq25] vac_adc: %d mV\n", vac_adc * 2);
  printf("[bq25] vbat_adc: %d mV\n", vbat_adc * 2);
  printf("[bq25] iac_adc: %f mA\n", ((float)iac_adc) * 0.8);
  printf("[bq25] ibat_adc: %d mA\n", ibat_adc * 2);

  /*
    [bq25] charger_control: c9
    [bq25] charger_status_1: 08
    [bq25] charger_status_2: b0
    [bq25] charger_status_3: 00
    [bq25] fault_status: 00
    [bq25] charger_flag_1: 48
    [bq25] charger_flag_2: 90
    [bq25] pin_control: c0
   */

  //bq25756_write_byte(0x14, 1);

  return vac_adc * 2;
}

#define BQ76922_ADDR 0x08

uint8_t bq76922_read_byte(i2c_inst_t* i2c, uint8_t addr)
{
  uint8_t buf;
  i2c_write_blocking(i2c, BQ76922_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c, BQ76922_ADDR, &buf, 1, false);
  return buf;
}

uint16_t bq76922_read_u16(i2c_inst_t* i2c, uint8_t addr)
{
  uint8_t buf[2] = {0,0};
  i2c_write_blocking(i2c, BQ76922_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c, BQ76922_ADDR, buf, 2, false);
  return (uint16_t)((buf[0]) | buf[1]<<8);
}

void bq76922_write_byte(i2c_inst_t* i2c, uint8_t addr, uint8_t byte)
{
  uint8_t buf[2] = {addr, byte};
  i2c_write_blocking(i2c, BQ76922_ADDR, buf, 2, false);
}

void bq76922_write_i16(i2c_inst_t* i2c, uint8_t addr, int16_t word)
{
  uint8_t buf[3] = {addr, word&0xff, word>>8};
  i2c_write_blocking(i2c, BQ76922_ADDR, buf, 3, false);
}

unsigned char bq76922_checksum(unsigned char *ptr, unsigned char len)
{
    unsigned char i;
    unsigned char checksum = 0;

    for (i = 0; i < len; i++) checksum += ptr[i];

    checksum = 0xff & ~checksum;

    return (checksum);
}

void bq76922_set_reg(i2c_inst_t* i2c, uint16_t reg_addr, uint32_t reg_data, uint8_t datalen)
{
  uint8_t TX_Buffer[3] = {0x00, 0x00, 0x00};
  uint8_t TX_RegData[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  //TX_RegData in little endian format
  TX_RegData[1] = reg_addr & 0xff;
  TX_RegData[2] = (reg_addr >> 8) & 0xff;
  TX_RegData[3] = reg_data & 0xff;  // 1st byte of data

  switch (datalen) {
  case 1:

    TX_RegData[0] = 0x3e;
    i2c_write_blocking(i2c, BQ76922_ADDR, TX_RegData, 4, false);
    sleep_ms(2);

    TX_Buffer[0] = 0x60;
    TX_Buffer[1] = bq76922_checksum(&TX_RegData[1], 3);
    TX_Buffer[2] = 0x05;  //combined length of register address and data
    i2c_write_blocking(i2c, BQ76922_ADDR, TX_Buffer, 3, false);
    sleep_ms(2);
    break;

  case 2:
    TX_RegData[4] = (reg_data >> 8) & 0xff;
    sleep_ms(2);
    i2c_write_blocking(i2c, BQ76922_ADDR, TX_RegData, 5, false);

    TX_Buffer[0] = 0x60;
    TX_Buffer[1] = bq76922_checksum(&TX_RegData[1], 4);
    TX_Buffer[2] = 0x06;  //combined length of register address and data
    i2c_write_blocking(i2c, BQ76922_ADDR, TX_Buffer, 3, false);
    sleep_ms(2);
    break;
  }
}

void monitor_setup(i2c_inst_t* i2c);

int monitor_read_subcommand(i2c_inst_t* i2c, uint8_t subcmd, uint8_t* buf, int len) {
  int tries = 0;
  int success = 0;

  bq76922_write_byte(i2c, 0x3e, subcmd);
  bq76922_write_byte(i2c, 0x3f, 0x00);

  while (tries < 10) {
    uint8_t tmp1 = bq76922_read_byte(i2c, 0x3e);
    uint8_t tmp2 = bq76922_read_byte(i2c, 0x3f);
    if (tmp1 != 0xff || tmp2 != 0xff) {
      success = 1;
      break;
    }
    sleep_ms(10);
  }
  if (!success) {
    return 0;
  }

  // TODO check checksum
  for (int i=0; i<len; i++) {
    buf[i] = bq76922_read_byte(i2c, 0x40+i);
  }
  return 1;
}

int monitor_configure(i2c_inst_t* i2c) {
  int id = 0;
  if (i2c == i2c1) id = 1;

  // subcommand: lo to 0x3e, hi to 0x3f
  // read 0x3e, 0x3f. if == 0xff, busy
  //                  if == written subcommand, done
  // read 0x61 response length
  // read 0x40... response length (max. up to 0x60)
  // later: check checksum (at 0x60). checksum includes 0x3e, 0x3f
  // don't read checksum and length at the same time (auto-increment stuff)

  // later, we can write defaults to OTP memory

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

  if (id == 0) {
    report_cells_v[0] = cell1_mv;
    report_cells_v[1] = cell2_mv;
    report_cells_v[2] = cell4_mv;
    report_cells_v[3] = cell5_mv;
  } else {
    report_cells_v[4] = cell1_mv;
    report_cells_v[5] = cell2_mv;
    report_cells_v[6] = cell4_mv;
    report_cells_v[7] = cell5_mv;
  }

  if (pack_mv == 0) {
    // pack not active
    return 0;
  }

  // TODO average pack 1 + 2
  report_volts = stack_mv/100.0; // default unit is centivolts
  report_current = cc2_ma/1000.0; // default unit is mA

  uint8_t manufacturing_status = 0;
  monitor_read_subcommand(i2c, 0x57, &manufacturing_status, 1);
  if (!(manufacturing_status & (1<<4))) {
    // FETs not enabled, setup the chip
    monitor_setup(i2c);
  }

  // 0x0097: FET_CONTROL

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
  float temp_int_k = temp_int_lo|(temp_int_hi<<8);
  float temp_ext_k = temp_ext_lo|(temp_ext_hi<<8);

  // balance above 3700mV
  // also interesting: https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1245177/bq76942-cell-balancing-not-activating
  //bq76922_write_byte(0x83, 16|8|0|2|1);
  //bq76922_write_i16(0x84, 3700);

  uint8_t bal_active_cells[2] = {0,0};
  uint8_t bal_status1[2] = {0,0};

  monitor_read_subcommand(i2c, 0x83, bal_active_cells, 2);
  monitor_read_subcommand(i2c, 0x85, bal_status1, 2);

  printf("\n---------------------------\n[bq76:%d] c1 mV: %f\n", id, cell1_mv);
  printf("[bq76] c2 mV: %f\n", cell2_mv);
  printf("[bq76] c3 mV: %f\n", cell3_mv);
  printf("[bq76] c4 mV: %f\n", cell4_mv);
  printf("[bq76] c5 mV: %f\n", cell5_mv);
  printf("[bq76] stack V: %f\n", report_volts);
  printf("[bq76] pack V: %f\n", pack_mv/100.0);
  printf("[bq76] ld V: %f\n", ld_mv/100.0);
  printf("[bq76] cc2 A: %f\n", report_current);
  printf("[bq76:%d] control_status: %02x\n", id, control_status);
  printf("[bq76:%d] manufacturing_status: %02x\n", id, manufacturing_status);
  printf("[bq76] `--     FET_EN: %d\n", !!(manufacturing_status & (1<<4)));
  printf("[bq76] `--      PF_EN: %d\n", !!(manufacturing_status & (1<<6)));
  printf("[bq76] `--   DSG_TEST: %d\n", !!(manufacturing_status & (1<<2)));
  printf("[bq76] `--   CHG_TEST: %d\n", !!(manufacturing_status & (1<<1)));
  printf("[bq76] `--  PCHG_TEST: %d\n", !!(manufacturing_status & (1<<0)));
  printf("[bq76] `--  PDSG_TEST: %d\n", !!(manufacturing_status & (1<<5)));
  printf("[bq76:%d] battery_status: %04x\n", id, battery_status);
  printf("[bq76] `--  SLEEP: %d\n", !!(battery_status & (1<<15)));
  printf("[bq76] `-- SD_CMD: %d\n", !!(battery_status & (1<<13)));
  printf("[bq76] `--     PF: %d\n", !!(battery_status & (1<<12)));
  printf("[bq76] `--     SS: %d\n", !!(battery_status & (1<<11)));
  printf("[bq76] `--   FUSE: %d\n", !!(battery_status & (1<<10)));
  printf("[bq76] `--   SEC1: %d\n", !!(battery_status & (1<<9)));
  printf("[bq76] `--   SEC0: %d\n", !!(battery_status & (1<<8)));
  printf("[bq76] `--   OTPB: %d\n", !!(battery_status & (1<<7)));
  printf("[bq76] `--   OTPW: %d\n", !!(battery_status & (1<<6)));
  printf("[bq76] `-- COWCHK: %d\n", !!(battery_status & (1<<5)));
  printf("[bq76] `--     WD: %d\n", !!(battery_status & (1<<4)));
  printf("[bq76] `--    POR: %d\n", !!(battery_status & (1<<3)));
  printf("[bq76] `-- SLEEPE: %d\n", !!(battery_status & (1<<2)));
  printf("[bq76] `-- PCHG_M: %d\n", !!(battery_status & (1<<1)));
  printf("[bq76] `-- CFGUPD: %d\n", !!(battery_status & (1<<0)));
  printf("[bq76:%d] fet_status: %02x\n", id, fet_status);
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
  printf("[bq76] temp_int: %f C\n", (temp_int_k-273.15)/100.0);
  printf("[bq76] temp_ext: %f C\n", (temp_ext_k-273.15)/100.0);

  printf("[bq76] bal_active_cells: %02x,%02x\n", bal_active_cells[0],bal_active_cells[1]);
  printf("[bq76] bal_status1: %d,%d sec\n", bal_status1[0], bal_status1[1]);

  return 1;
}

void mon_all_fets_off(i2c_inst_t* i2c) {
  printf("[bq76] turning all fets off...\n");
  bq76922_write_byte(i2c, 0x3e, 0x95);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_all_fets_on(i2c_inst_t* i2c) {
  printf("[bq76] turning all fets on...\n");
  bq76922_write_byte(i2c, 0x3e, 0x96);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_toggle_fet_en(i2c_inst_t* i2c) {
  printf("[bq76] fet_en toggle...\n");
  bq76922_write_byte(i2c, 0x3e, 0x22);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_fet_test(i2c_inst_t* i2c) {
  printf("[bq76] fet test...\n");

  bq76922_write_byte(i2c, 0x3e, 0x1c);
  bq76922_write_byte(i2c, 0x3f, 0x00);
  bq76922_write_byte(i2c, 0x3e, 0x1e);
  bq76922_write_byte(i2c, 0x3f, 0x00);
  bq76922_write_byte(i2c, 0x3e, 0x1f);
  bq76922_write_byte(i2c, 0x3f, 0x00);
  bq76922_write_byte(i2c, 0x3e, 0x20);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_sleep_off(i2c_inst_t* i2c) {
  printf("[bq76] turning sleep off...\n");
  bq76922_write_byte(i2c, 0x3e, 0x9a);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_sleep_on(i2c_inst_t* i2c) {
  printf("[bq76] turning sleep on...\n");
  bq76922_write_byte(i2c, 0x3e, 0x99);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void monitor_setup(i2c_inst_t* i2c) {
  int id = 0;
  if (i2c == i2c1) id = 1;

  printf("[bq76:%d] monitor_setup begin\n", id);

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

  if (cfgupd) {
    // 4 cells, one missing in the middle
    uint8_t vcell_mode = 16 | 8 | 0 | 2 | 1;
    bq76922_set_reg(i2c, 0x9304, vcell_mode, 1);

    // TODO: read back and check these values

    // disable all FET protections :0
    /*bq76922_set_reg(0x9265, 0, 1);
      bq76922_set_reg(0x9266, 0, 1);
      bq76922_set_reg(0x9267, 0, 1);
      bq76922_set_reg(0x9269, 0, 1);
      bq76922_set_reg(0x926a, 0, 1);
      bq76922_set_reg(0x926b, 0, 1);*/

    // FET options
    bq76922_set_reg(i2c, 0x9308, (1<<4)|(1<<3)|(1<<2)|(1<<1)|(1<<0), 1);

    // enable normal FET control in Mfg Status Init
    // FIXME doesn't seem to work
    bq76922_set_reg(i2c, 0x9343, (1<<6)|(1<<4), 2);

    // enable balancing
    // TODO: investigate
    bq76922_set_reg(i2c, 0x9335, (0<<4)|(1<<3)|(1<<2)|(1<<1)|(1<<0), 1);

    // exit config update mode
    bq76922_write_byte(i2c, 0x3e, 0x92);
    bq76922_write_byte(i2c, 0x3f, 0x00);

    battery_status = bq76922_read_u16(i2c, 0x12);
    printf("[bq76] `-- CFGUPD (expect 0): %d\n", !!(battery_status & (1<<0)));
  }

  mon_sleep_off(i2c);
  mon_toggle_fet_en(i2c);

  printf("[bq76:%d] monitor_setup done\n", id);
}

void init_spi_client();

void turn_som_power_on() {
  init_spi_client();

  // latch
  gpio_put(PIN_PWREN_LATCH, 1);

  gpio_put(PIN_LED_B, 1);

  set_boot_magic();

  printf("# [action] turn_som_power_on\n");
  gpio_put(PIN_5V_ENABLE, 1);
  sleep_ms(10);
  gpio_put(PIN_3V3_ENABLE, 1);
  sleep_ms(10);

  // done with latching
  gpio_put(PIN_PWREN_LATCH, 0);

  som_is_powered = true;
}

void turn_som_power_off() {
  init_spi_client();

  // latch
  gpio_put(PIN_PWREN_LATCH, 1);

  gpio_put(PIN_LED_B, 0);

  clear_boot_magic();

  printf("# [action] turn_som_power_off\n");

  gpio_put(PIN_5V_ENABLE, 0);
  sleep_ms(10);
  gpio_put(PIN_3V3_ENABLE, 0);

  // done with latching
  gpio_put(PIN_PWREN_LATCH, 0);

  som_is_powered = false;
}

void som_wake() {
  uart_puts(uart0, "wake\r\n");
}

#define ST_EXPECT_DIGIT_0 0
#define ST_EXPECT_DIGIT_1 1
#define ST_EXPECT_DIGIT_2 2
#define ST_EXPECT_DIGIT_3 3
#define ST_EXPECT_CMD     4
#define ST_SYNTAX_ERROR   5
#define ST_EXPECT_RETURN  6
#define ST_EXPECT_MAGIC   7

char remote_cmd = 0;
uint8_t remote_arg = 0;
unsigned char cmd_state = ST_EXPECT_DIGIT_0;
unsigned int cmd_number = 0;
int cmd_echo = 0;
char uart_buffer[255] = {0};

// chr: input character
void handle_commands(char chr) {
  if (cmd_echo) {
    sprintf(uart_buffer, "%c", chr);
    uart_puts(UART_ID, uart_buffer);
  }

  // states:
  // 0-3 digits of optional command argument
  // 4   command letter expected
  // 5   syntax error (unexpected character)
  // 6   command letter entered

  if (cmd_state>=ST_EXPECT_DIGIT_0 && cmd_state<=ST_EXPECT_DIGIT_3) {
    // read number or command
    if (chr >= '0' && chr <= '9') {
      cmd_number*=10;
      cmd_number+=(chr-'0');
      cmd_state++;
    } else if ((chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z')) {
      // command entered instead of digit
      remote_cmd = chr;
      cmd_state = ST_EXPECT_RETURN;
    } else if (chr == '\n' || chr == ' ') {
      // ignore newlines or spaces
    } else if (chr == '\r') {
      sprintf(uart_buffer, "error:syntax\r\n");
      uart_puts(UART_ID, uart_buffer);
      cmd_state = ST_EXPECT_DIGIT_0;
      cmd_number = 0;
    } else {
      // syntax error
      cmd_state = ST_SYNTAX_ERROR;
    }
  }
  else if (cmd_state == ST_EXPECT_CMD) {
    // read command
    if ((chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z')) {
      remote_cmd = chr;
      cmd_state = ST_EXPECT_RETURN;
    } else {
      cmd_state = ST_SYNTAX_ERROR;
    }
  }
  else if (cmd_state == ST_SYNTAX_ERROR) {
    // syntax error
    if (chr == '\r') {
      sprintf(uart_buffer, "error:syntax\r\n");
      uart_puts(UART_ID, uart_buffer);
      cmd_state = ST_EXPECT_DIGIT_0;
      cmd_number = 0;
    }
  }
  else if (cmd_state == ST_EXPECT_RETURN) {
    if (chr == '\n' || chr == ' ') {
      // ignore newlines or spaces
    }
    else if (chr == '\r') {
      if (cmd_echo) {
        // FIXME
        sprintf(uart_buffer,"\n");
        uart_puts(UART_ID, uart_buffer);
      }

      // execute
      if (remote_cmd == 'p') {
        // toggle system power and/or reset imx
        if (cmd_number == 0) {
          turn_som_power_off();
          sprintf(uart_buffer,"system: off\r\n");
          uart_puts(UART_ID, uart_buffer);
        } else if (cmd_number == 2) {
          //reset_som();
          sprintf(uart_buffer,"system: reset\r\n");
          uart_puts(UART_ID, uart_buffer);
        } else {
          turn_som_power_on();
          sprintf(uart_buffer,"system: on\r\n");
          uart_puts(UART_ID, uart_buffer);
        }
      }
      else if (remote_cmd == 'a') {
        // TODO
        // get system current (mA)
        sprintf(uart_buffer,"%d\r\n",0);
        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 'v' && cmd_number>=0 && cmd_number<=0) {
        // TODO
        // get cell voltage
        sprintf(uart_buffer,"%d\r\n",0);
        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 'V') {
        // TODO
        // get system voltage
        sprintf(uart_buffer,"%d\r\n",0);
        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 's') {
        // TODO
        sprintf(uart_buffer,FW_REV"normal,%d,%d,%d\r\n",0,0,0);
        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 'u') {
        // TODO
        // turn reporting to i.MX on or off
      }
      else if (remote_cmd == 'w') {
        // wake SoC
        som_wake();
        sprintf(uart_buffer,"system: wake\r\n");
        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 'c') {
        // get status of cells, current, voltage, fuel gauge
        int mA = (int)(report_current*1000.0);
        char mA_sign = ' ';
        if (mA<0) {
          mA = -mA;
          mA_sign = '-';
        }
        int mV = (int)(report_volts*1000.0);
        sprintf(uart_buffer,"%02d %02d %02d %02d %02d %02d %02d %02d mA%c%04dmV%05d %3d%% P%d\r\n",
                (int)(report_cells_v[0]/100),
                (int)(report_cells_v[1]/100),
                (int)(report_cells_v[2]/100),
                (int)(report_cells_v[3]/100),
                (int)(report_cells_v[4]/100),
                (int)(report_cells_v[5]/100),
                (int)(report_cells_v[6]/100),
                (int)(report_cells_v[7]/100),
                mA_sign,
                mA,
                mV,
                report_capacity_percentage,
                som_is_powered?1:0);

        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 'S') {
        // TODO
        // get charger system cycles in current state
        sprintf(uart_buffer, "%d\r\n", 0);
        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 'C') {
        // TODO
        // get battery capacity (mAh)
        sprintf(uart_buffer,"%d/%d/%d\r\n",0,0,0);
        uart_puts(UART_ID, uart_buffer);
      }
      else if (remote_cmd == 'e') {
        // toggle serial echo
        cmd_echo = cmd_number?1:0;
      }
      else {
        sprintf(uart_buffer, "error:command\r\n");
        uart_puts(UART_ID, uart_buffer);
      }

      cmd_state = ST_EXPECT_DIGIT_0;
      cmd_number = 0;
    } else {
      cmd_state = ST_SYNTAX_ERROR;
    }
  }
}

#define SPI_BUF_LEN 0x8
uint8_t spi_buf[SPI_BUF_LEN];
unsigned char spi_cmd_state = ST_EXPECT_MAGIC;
unsigned char spi_command = '\0';
uint8_t spi_arg1 = 0;

void init_spi_client() {
  gpio_set_function(PIN_SOM_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_SS0, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_SCK, GPIO_FUNC_SPI);

  spi_init(spi1, 400 * 1000);
  // we don't appreciate the wording, but it's the API we are given
  spi_set_slave(spi1, true);
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);

  printf("# [spi] init_spi_client done\n");
}

/**
 * @brief SPI command from imx poll function
 *
 * Ported from MNT Reform reform2-lpc-fw.
 */
void handle_spi_commands() {
  int len = 0;
  int all_zeroes = 1;

  while (spi_is_readable(spi1) && len < SPI_BUF_LEN) {
    // 0x00 is "repeated tx data"
    spi_read_blocking(spi1, 0x00, &spi_buf[len], 1);
    if (spi_buf[len] != 0) all_zeroes = 0;
    len++;
  }

  if (len == 0) {
    return;
  }

  //printf("# [spi] rx (len = %d): %02x %02x %02x %02x %02x %02x %02x %02x\n", len, spi_buf[0], spi_buf[1], spi_buf[2], spi_buf[3], spi_buf[4], spi_buf[5], spi_buf[6], spi_buf[7]);

  // states:
  // 0   arg1 byte expected
  // 4   command byte expected
  // 6   execute command
  // 7   magic byte expected
  for (uint8_t s = 0; s < len; s++) {
    if (spi_cmd_state == ST_EXPECT_MAGIC) {
      // magic byte found, prevents garbage data
      // in the bus from triggering a command
      if (spi_buf[s] == 0xb5) {
        spi_cmd_state = ST_EXPECT_CMD;
      }
    }
    else if (spi_cmd_state == ST_EXPECT_CMD) {
      // read command
      spi_command = spi_buf[s];
      spi_cmd_state = ST_EXPECT_DIGIT_0;
    }
    else if (spi_cmd_state == ST_EXPECT_DIGIT_0) {
      // read arg1 byte
      spi_arg1 = spi_buf[s];
      spi_cmd_state = ST_EXPECT_RETURN;
    }
    //printf("# [spi] after 0x%02x (pos %d): state %d cmd %c (%02x) arg %d\n", spi_buf[s], s, spi_cmd_state, spi_command, spi_command, spi_arg1);
  }

  if (spi_cmd_state == ST_EXPECT_MAGIC && !all_zeroes) {
    // reset SPI0 block
    // this is a workaround for confusion with
    // software spi from BPI-CM4 where we get
    // bit-shifted bytes

    init_spi_client();
    spi_cmd_state = ST_EXPECT_MAGIC;
    spi_command = 0;
    spi_arg1 = 0;
    return;
  }

  if (spi_cmd_state != ST_EXPECT_RETURN) {
    // waiting for more data
    return;
  }

  printf("# [spi] exec: '%c' 0x%02x\n", spi_command, spi_arg1);

  // clear receive buffer, reuse as send buffer
  memset(spi_buf, 0, SPI_BUF_LEN);

  // execute power state command
  if (spi_command == 'p') {
    // toggle system power and/or reset imx
    if (spi_arg1 == 1) {
      turn_som_power_off();
    }
    if (spi_arg1 == 2) {
      turn_som_power_on();
    }
    if (spi_arg1 == 3) {
      // TODO
      //reset_som();
    }

    spi_buf[0] = som_is_powered;
  }
  // return firmware version and api info
  else if (spi_command == 'f') {
    if(spi_arg1 == 0) {
      memcpy(spi_buf, FW_STRING1, 8);
    }
    else if(spi_arg1 == 1) {
      memcpy(spi_buf, FW_STRING2, 2);
    }
    else {
      memcpy(spi_buf, FW_STRING3, 8);
    }
  }
  // execute status query command
  else if (spi_command == 'q') {
    uint8_t percentage = (uint8_t)report_capacity_percentage;
    int16_t voltsInt = (int16_t)(report_volts*1000.0);
    int16_t currentInt = (int16_t)(report_current*1000.0);

    spi_buf[0] = (uint8_t)voltsInt;
    spi_buf[1] = (uint8_t)(voltsInt >> 8);
    spi_buf[2] = (uint8_t)currentInt;
    spi_buf[3] = (uint8_t)(currentInt >> 8);
    spi_buf[4] = (uint8_t)percentage;
    spi_buf[5] = (uint8_t)0; // TODO "state" not implemented
    spi_buf[6] = (uint8_t)0;
  }
  // get cell voltage
  else if (spi_command == 'v') {
    uint16_t volts = 0;
    uint8_t cell1 = 0;

    if (spi_arg1 == 1) {
      cell1 = 4;
    }

    for (uint8_t c = 0; c < 4; c++) {
      volts = report_cells_v[c + cell1];
      spi_buf[c*2] = (uint8_t)volts;
      spi_buf[(c*2)+1] = (uint8_t)(volts >> 8);
    }
  }
  // get calculated capacity
  else if (spi_command == 'c') {
    uint16_t cap_accu = (uint16_t) report_capacity_max_ampsecs / 3.6;
    uint16_t cap_min = (uint16_t) report_capacity_min_ampsecs / 3.6;
    uint16_t cap_max = (uint16_t) report_capacity_max_ampsecs / 3.6;

    spi_buf[0] = (uint8_t)cap_accu;
    spi_buf[1] = (uint8_t)(cap_accu >> 8);
    spi_buf[2] = (uint8_t)cap_min;
    spi_buf[3] = (uint8_t)(cap_min >> 8);
    spi_buf[4] = (uint8_t)cap_max;
    spi_buf[5] = (uint8_t)(cap_max >> 8);
  }
  else if (spi_command == 'u') {
    // not implemented
  }

  // FIXME: if we don't reset, SPI wants to transact the amount of bytes
  // that we read above for unknown reasons
  init_spi_client();

  if (som_is_powered) {
    spi_write_blocking(spi1, spi_buf, SPI_BUF_LEN);
  }

  spi_cmd_state = ST_EXPECT_MAGIC;
  spi_command = 0;
  spi_arg1 = 0;

  return;
}

void on_uart_rx() {
  while (uart_is_readable(UART_ID)) {
    handle_commands(uart_getc(UART_ID));
  }
}

int main() {
  //set_sys_clock_48mhz();

  stdio_init_all();
  init_spi_client();

  printf("# [reset] cause: %#.8x\n", vreg_and_chip_reset_hw->chip_reset);
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

  // I2C1
  gpio_set_function(PIN_SDA1, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL1, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA1, PIN_SCL1, GPIO_FUNC_I2C));
  i2c_init(i2c1, 100 * 1000);

  gpio_init(PIN_LED_R);
  gpio_init(PIN_LED_G);
  gpio_init(PIN_LED_B);
  gpio_set_dir(PIN_LED_R, 1);
  gpio_set_dir(PIN_LED_G, 1);
  gpio_set_dir(PIN_LED_B, 1);

  gpio_init(PIN_3V3_ENABLE);
  gpio_init(PIN_5V_ENABLE);
  gpio_set_dir(PIN_3V3_ENABLE, 1);
  gpio_set_dir(PIN_5V_ENABLE, 1);
  gpio_put(PIN_3V3_ENABLE, 0);
  gpio_put(PIN_5V_ENABLE, 0);

  gpio_init(PIN_PWREN_LATCH);
  gpio_set_dir(PIN_PWREN_LATCH, 1);
  gpio_put(PIN_PWREN_LATCH, 0);

  gpio_init(PIN_CHRG_CE);
  gpio_put(PIN_CHRG_CE, 0);

  gpio_put(PIN_LED_R, 0);
  gpio_put(PIN_LED_G, 0);
  gpio_put(PIN_LED_B, 0);

  // FIXME this is now on gpio extender
  //gpio_init(PIN_USB_SRC_ENABLE);
  //gpio_set_dir(PIN_USB_SRC_ENABLE, 1);
  //gpio_put(PIN_USB_SRC_ENABLE, 0);

  // if this is a warm boot, then we need to avoid latching the PWR and display
  // pins.
  if (syscon_warm_boot()) {
      printf("# [reset] watchdog scratch had valid on magic, not latching power.\n");
      som_is_powered = true;
  } else {
      gpio_put(PIN_PWREN_LATCH, 1);
      gpio_put(PIN_PWREN_LATCH, 0);
  }

  unsigned int t = 0;
  unsigned int t_report = 0;

  int state = 0;
  int request_sent = 0;
  uint8_t rxdata[2];

  union pd_msg tx;
  int tx_id_count = 0;
  union pd_msg rx_msg;

  int power_objects = 0;
  int max_voltage = 0;

  sleep_ms(1000);

#ifdef FACTORY_MODE
  // in factory mode, turn on power immediately to be able to flash
  // the keyboard
  turn_som_power_on();
#endif

  printf("# [next_sysctl] entering main loop.\n");

  while (true) {
    // handle commands from keyboard
    while (uart_is_readable(UART_ID)) {
      handle_commands(uart_getc(UART_ID));
    }

    handle_spi_commands();

#ifdef ACM_ENABLED
    // handle commands over usb serial
    int usb_c = getchar_timeout_us(0);
    if (usb_c != PICO_ERROR_TIMEOUT) {
      printf("# [acm_command] '%c'\n", usb_c);
      if (usb_c == '1') {
        turn_som_power_on();
      }
      else if (usb_c == '0') {
        turn_som_power_off();
      }
      else if (usb_c == 'p') {
        print_pack_info = !print_pack_info;
      }
      else if (usb_c == 'i') {
        i2c_scan(i2c0);
      }
      else if (usb_c == 'I') {
        i2c_scan(i2c1);
      }
      else if (usb_c == 'q') {
        mon_all_fets_off(i2c0);
      }
      else if (usb_c == 'w') {
        mon_all_fets_on(i2c0);
      }
      else if (usb_c == 's') {
        mon_sleep_off(i2c0);
      }
      else if (usb_c == 'S') {
        mon_sleep_off(i2c1);
      }
      else if (usb_c == 'x') {
        monitor_setup(i2c0);
      }
      else if (usb_c == 'X') {
        monitor_setup(i2c1);
      }
      else if (usb_c == 'f') {
        mon_toggle_fet_en(i2c0);
      }
      else if (usb_c == 'F') {
        mon_toggle_fet_en(i2c1);
      }
    }
#endif

    if (state == 0) {
      //printf("# [next_sysctl] state 0\n");
      gpio_put(PIN_LED_R, 0);
      power_objects = 0;
      request_sent = 0;

      // by default, we output 5V on VUSB
      //gpio_put(PIN_USB_SRC_ENABLE, 1);

      //printf("# [pd] state 0\n");
      // probe FUSB302BMPX
      if (i2c_read_timeout_us(i2c0, FUSB_ADDR, rxdata, 1, false, I2C_TIMEOUT)) {
        // 1. set auto GoodCRC
        // AUTO_CRC in Switches1
        // Address: 03h; Reset Value: 0b0010_0000

        printf("# [pd] FUSB probed.\n");

        fusb_write_byte(FUSB_RESET, FUSB_RESET_SW_RES);

        sleep_us(10);

        // turn on all power
        fusb_write_byte(FUSB_POWER, 0x0F);
        // automatic retransmission
        fusb_write_byte(FUSB_CONTROL3,
                        FUSB_CONTROL3_AUTO_HARDRESET |
                        FUSB_CONTROL3_AUTO_SOFTRESET |
                        (3<<FUSB_CONTROL3_N_RETRIES_SHIFT) |
                        FUSB_CONTROL3_AUTO_RETRY);
        // flush rx buffer
        fusb_write_byte(FUSB_CONTROL1, FUSB_CONTROL1_RX_FLUSH);

        // pdwn means pulldown. 0 = no pull down

        /* Measure CC1 */
        fusb_write_byte(FUSB_SWITCHES0, 4|2|1); //  MEAS_CC1|PDWN2   |PDWN1
        sleep_us(250);
        uint8_t cc1 = fusb_read_byte(FUSB_STATUS0) & FUSB_STATUS0_BC_LVL;

        printf("# [pd] CC1: %d\n", cc1);

        /* Measure CC2 */
        fusb_write_byte(FUSB_SWITCHES0, 8|2|1); //  MEAS_CC2|PDWN2   |PDWN1
        sleep_us(250);
        uint8_t cc2 = fusb_read_byte(FUSB_STATUS0) & FUSB_STATUS0_BC_LVL;

        printf("# [pd] CC2: %d\n", cc2);

        // detect orientation
        if (cc1 > cc2) {
          fusb_write_byte(FUSB_SWITCHES1,    4|1); //          |AUTO_CRC|TXCC1
          fusb_write_byte(FUSB_SWITCHES0,  4|2|1); //  MEAS_CC1|PDWN2   |PDWN1
        } else {
          fusb_write_byte(FUSB_SWITCHES1,    4|2); //          |AUTO_CRC|TXCC2
          fusb_write_byte(FUSB_SWITCHES0,  8|2|1); //  MEAS_CC2|PDWN2   |PDWN1
        }

        printf("# [pd] switches set.\n");

        fusb_write_byte(FUSB_RESET, FUSB_RESET_PD_RESET);

        printf("# [pd] auto hard/soft reset and retries set.\n");

        t = 0;
        state = 1;
      } else {
        if (t > 100) {
          printf("# [pd] state 0: fusb timeout.\n");
          t = 0;
        }
      }

    } else if (state == 1) {
      //printf("[next-sysctl] state 1\n");

      if (t>300) {
        printf("# [pd] state 1, timeout.\n");
        t = 0;

        // without batteries, the system dies here (brownout?)
        // but the charger might have set up the requested voltage anyway
        //if (input_voltage < 6) {
          fusb_write_byte(FUSB_CONTROL3, (1<<6) | (1<<4) | (1<<3) | (3<<1) | 1);
          sleep_ms(1);
          fusb_write_byte(FUSB_CONTROL3, (1<<4) | (1<<3) | (3<<1) | 1);
          state = 0;
          //  } else {*/
        //state = 3;
          //}
      }

      int res = fusb_read_message(&rx_msg);

      if (!res) {
        //printf("# [pd] s1: charger responds, turning off USB_SRC\n");
        // if a charger is responding, turn off our 5V output
        //gpio_put(PIN_USB_SRC_ENABLE, 0);

        uint8_t msgtype = PD_MSGTYPE_GET(&rx_msg);
        uint8_t numobj = PD_NUMOBJ_GET(&rx_msg);
        if (msgtype == PD_MSGTYPE_SOURCE_CAPABILITIES) {
          max_voltage = 0;
          for (int i=0; i<numobj; i++) {
            uint32_t pdo = rx_msg.obj[i];

            if ((pdo & PD_PDO_TYPE) == PD_PDO_TYPE_FIXED) {
              int voltage = print_src_fixed_pdo(i+1, pdo);
              // FIXME voltage
              if (voltage > max_voltage && voltage <= 12) {
                power_objects = i+1;
                max_voltage = voltage;
              }
            } else {
              printf("# [pd] state 1, not a fixed PDO: 0x%08x\n", pdo);
            }
          }
          if (!request_sent) {
            state = 2;
            t = 0;
          }
        } else if (msgtype == PD_MSGTYPE_PS_RDY) {
          // power supply is ready
          printf("# [pd] state 1, power supply ready.\n");
          request_sent = 0;
          t = 0;
          state = 3;
        } else {
          printf("# [pd] state 1, msg type: 0x%x numobj: %d\n", msgtype, numobj);
        }
      } else {
        //sleep_ms(1);
        //printf("# [pd] state 1, no message\n");
      }
    } else if (state == 2) {
      printf("# [pd] state 2, requesting PO %d, %d V\n", power_objects, max_voltage);

      tx.hdr = PD_MSGTYPE_REQUEST | PD_NUMOBJ(1) | PD_DATAROLE_UFP | PD_POWERROLE_SINK | PD_SPECREV_2_0;

      tx.hdr &= ~PD_HDR_MESSAGEID;
      tx.hdr |= (tx_id_count % 8) << PD_HDR_MESSAGEID_SHIFT;

      int current = 100;

      tx.obj[0] = PD_RDO_FV_MAX_CURRENT_SET(current)
        | PD_RDO_FV_CURRENT_SET(current)
        | PD_RDO_NO_USB_SUSPEND | PD_RDO_OBJPOS_SET(power_objects); // FIXME

      fusb_send_message(&tx);

      printf("# [pd] state 2, request sent.\n");

      tx_id_count++;

      t = 0;
      request_sent = 1;
      state = 1;
    } else if (state == 3) {
      gpio_put(PIN_LED_R, 1);
      //gpio_put(PIN_USB_SRC_ENABLE, 0);

      // charging
      sleep_ms(1);

      // running
      if (t>200) {
        printf("# [pd] state 3.\n");

        int input_mv = charger_configure();

        if (input_mv < 5100) {
          printf("# [pd] input voltage below threshold, renegotiate.\n");
          state = 0;
        }

        t = 0;
      }
    }

    sleep_ms(10);
    t++;
    t_report++;

    if (t_report > 200) {
      monitor_configure(i2c0);
      monitor_configure(i2c1);
      charger_configure();
      t_report = 0;
    }
  }

  return 0;
}
