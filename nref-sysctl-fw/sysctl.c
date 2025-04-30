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
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
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

#define PIN_BAT1_ALERT 6
#define PIN_BAT2_ALERT 7

#define PIN_SOM_MOSI 8
#define PIN_SOM_SS0 9
#define PIN_SOM_SCK 10
#define PIN_SOM_MISO 11

#define PIN_HSTX_D0P 12
#define PIN_HSTX_D0N 13
#define PIN_HSTX_DCKP 14
#define PIN_HSTX_DCKN 15
#define PIN_HSTX_D2P 16
#define PIN_HSTX_D2N 17
#define PIN_HSTX_D1P 18
#define PIN_HSTX_D1N 19

#define PIN_LED_B 20
#define PIN_LED_R 21
#define PIN_LED_G 22

#define PIN_CHRG_CFG 23
#define PIN_CHRG_ALERT 24

#define PIN_BACKLIGHT_EN 25
#define PIN_BACKLIGHT_PWM 26

#define PIN_SOM_WAKE 27
#define PIN_SOM_UART_TX 28
#define PIN_SOM_UART_RX 29

// FIXME: the following are now on PCA9536DP (on SDA/SCL1)
// 3V3_ENABLE
// 5V_ENABLE
// HDMI_DP_SWITCH
// ~QON

// PCA9536DP GPIO extender (on motherboard, i2c1)
#define PCA9536_ADDR 0x41

// FUSB302B USB-PD controller (on usb-c pd board)
#define FUSB_ADDR 0x22

// PCAL6416AHF GPIO extender (on usb-c pd board)
#define PCAL_ADDR 0x20

// BQ25792 charger
#define BQ25792_ADDR 0x6b

// BQ76922 monitor on battery boards
#define BQ76922_ADDR 0x08

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

void pca9536_write_byte(uint8_t addr, uint8_t val) {
  uint8_t buf[2] = {addr, val};
  i2c_write_blocking(i2c1, PCA9536_ADDR, buf, 2, false);
}

uint8_t pca9536_read_byte(uint8_t addr) {
  uint8_t buf;
  i2c_write_blocking(i2c1, PCA9536_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c1, PCA9536_ADDR, &buf, 1, false);
  return buf;
}

static uint8_t gpio_ext_state;

#define GPIO_EXT_5V_EN 0
#define GPIO_EXT_3V3_EN 1
#define GPIO_EXT_HDMI_DP 2
#define GPIO_EXT_NQON 3

void gpio_ext_setup() {
  /*
    IO0: 5V_ENABLE
    IO1: 3V3_ENABLE
    IO2: HDMI_DP_SWITCH
    IO3: ~QON
  */

  gpio_ext_state = 0b0000;

  // config: all outputs
  pca9536_write_byte(3, 0b0000);
  // output port:
  pca9536_write_byte(1, gpio_ext_state);
}

void gpio_ext_enable(uint8_t bit) {
  gpio_ext_state |= (1<<bit);
  pca9536_write_byte(1, gpio_ext_state);
}
void gpio_ext_disable(uint8_t bit) {
  gpio_ext_state &= ~(1<<bit);
  pca9536_write_byte(1, gpio_ext_state);
}

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
  uint8_t buf_lsb[2] = {addr, word & 0xff};
  uint8_t buf_msb[2] = {addr, word>>8};

  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_msb, 2, false);
  addr++;
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_lsb, 2, false);
}

void bq25792_write_word_signed(uint8_t addr, int16_t word)
{
  uint8_t buf_lsb[2] = {addr, word & 0xff};
  uint8_t buf_msb[2] = {addr, word>>8};

  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_lsb, 2, false);
  addr++;
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_msb, 2, false);
}

void charger_configure() {
  // see https://www.ti.com/lit/ds/symlink/bq25792.pdf

  // TODO:
  // - REG00_Minimal_System_Voltage
  // - REG01_Charge_Voltage_Limit range: 3000mV - 18800mV. 16 bit reg
  //   - should be set to around 14.4-15.6? (4x3.9)
  //   - bit step 10mV
  // - REG05_Input_Voltage_Limit ?
  // - REG08_Precharge_Control
  // - REG09_Termination_Control
  // - REG0A_Re-charge_Control
  // - REG0E_Timer_Control
  // - REG0F_Charger_Control_0 (some interesting stuff here like ICO)
  // - REG14_Charger_Control_5 -> EN_IBAT (bit 5)

  // VREG = charge voltage

  bq25792_write_byte(0x00, (10000 - 2500) / 250); // 10.0V vsysmin, 250mV step, 2500mV offset
  bq25792_write_word(0x01, 15600 / 10); // charge voltage (conservative), VREG
  bq25792_write_word(0x03, 1500 / 10); // 2A charge current
  bq25792_write_word(0x06, 3000 / 10); // defaults to 3A @ reset

  // ADC control: 0x2e (default: 0x30)
  bq25792_write_byte(0x2e, (1<<7) | (0b00 << 4) ); // enable ADC at 15 bit (7=ADC_EN, 5:4=ADC_SAMPLE)

  // charger_control_0
  // bit7: EN_AUTO_IBATDIS
  // bit6: FORCE_IBATDIS
  // bit5: EN_CHG
  // bit4: EN_ICO
  // bit3: FORCE_ICO
  // bit2: EN_HIZ
  // bit1: EN_TERM
  bq25792_write_word(0x0f, 0b00100000);

  // charger_control_2
  // bit6: AUTO_INDET_EN (default on, D+/D- detection)
  bq25792_write_word(0x11, 0b00000000);

  // charger_control_5
  bq25792_write_word(0x14, 0b00111110);
}

int charger_status() {
  // charger_control_1
  // bit3: WD_RST
  // bit2-0: watchdog timeout
  bq25792_write_word(0x10, 0b00001000);

  uint8_t charger_status_0 = bq25792_read_byte(0x1b);
  uint8_t charger_status_1 = bq25792_read_byte(0x1c);
  uint8_t charger_status_2 = bq25792_read_byte(0x1d);
  uint8_t charger_status_3 = bq25792_read_byte(0x1e);
  uint8_t charger_status_4 = bq25792_read_byte(0x1f);
  uint8_t fault_status_0 = bq25792_read_byte(0x20);
  uint8_t fault_status_1 = bq25792_read_byte(0x21);
  uint8_t recharge_ctl = bq25792_read_byte(0x0a);

  uint8_t cell_count = (recharge_ctl >> 6) && 0b11;

  int16_t ibus_adc = bq25792_read_word_signed(0x31); // 1mA resolution
  int16_t ibat_adc = bq25792_read_word_signed(0x33); // 1mA resolution
  int16_t ilim = bq25792_read_word_signed(0x19)*10; // 10mA resolution
  uint16_t vbus_adc = bq25792_read_word(0x35); // 1mV resolution
  uint16_t vac1_adc = bq25792_read_word(0x37); // 1mV resolution
  uint16_t vac2_adc = bq25792_read_word(0x39); // 1mV resolution
  uint16_t vbat_adc = bq25792_read_word(0x3b); // 1mV resolution
  uint16_t vsys_adc = bq25792_read_word(0x3d); // 1mV resolution
  float tdie_adc = (float)bq25792_read_word_signed(0x41) * 0.5; // 0.5 celsius resolution

  printf("\n---------------------------\n");
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
  if ((charger_status_1 & 0b11100000) == 0) printf("[bq25] `-- not charging\n");
  if ((charger_status_1 & 0b11100000) == 1) printf("[bq25] `-- trickle charge\n");
  if ((charger_status_1 & 0b11100000) == 2) printf("[bq25] `-- pre-charge\n");
  if ((charger_status_1 & 0b11100000) == 3) printf("[bq25] `-- fast charge CC\n");
  if ((charger_status_1 & 0b11100000) == 4) printf("[bq25] `-- taper charge CV\n");
  if ((charger_status_1 & 0b11100000) == 5) printf("[bq25] `-- reserved\n");
  if ((charger_status_1 & 0b11100000) == 6) printf("[bq25] `-- top-off timer\n");
  if ((charger_status_1 & 0b11100000) == 7) printf("[bq25] `-- termination done\n");
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

  printf("[bq25] vbus: %d mV\n", vbus_adc);
  printf("[bq25] vac1: %d mV\n", vac1_adc);
  printf("[bq25] vbat: %d mV\n", vbat_adc);
  printf("[bq25] ibus: %d mA\n", ibus_adc);
  printf("[bq25] ibat: %d mA\n", ibat_adc);
  printf("[bq25] ilim: %d mA\n", ilim);
  printf("[bq25] tdie: %f C\n",  tdie_adc);
  printf("\n---------------------------\n");

  return vbus_adc;
}

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

void bq76922_write_u16(i2c_inst_t* i2c, uint8_t addr, uint16_t word)
{
  uint8_t buf[3] = {addr, word&0xff, word>>8};
  i2c_write_blocking(i2c, BQ76922_ADDR, buf, 3, false);
}

int bq76922_read_mem_u8(i2c_inst_t* i2c, uint16_t reg_addr, uint8_t* reg_data) {

  bq76922_write_byte(i2c, 0x3e, reg_addr & 0xff);
  bq76922_write_byte(i2c, 0x3f, reg_addr >> 8);
  sleep_ms(10);

  *reg_data = bq76922_read_byte(i2c, 0x40);

  int len = bq76922_read_byte(i2c, 0x61);
  int checksum = bq76922_read_byte(i2c, 0x60);

  printf("[bq76] read_mem_u8: %02x = %02x [len: %d checksum: %02x]\n", reg_addr, *reg_data, len, checksum);

  return 1;
}

void bq76922_write_mem_u8(i2c_inst_t* i2c, uint16_t reg_addr, uint8_t reg_data) {
  bq76922_write_byte(i2c, 0x3e, reg_addr & 0xff);
  bq76922_write_byte(i2c, 0x3f, reg_addr >> 8);

  bq76922_write_byte(i2c, 0x40, reg_data);

  // 5 = len
  uint32_t checksum = (~((reg_addr & 0xff) + (reg_addr >> 8) + reg_data)) & 0xff;
  bq76922_write_u16(i2c, 0x60, checksum | 5<<8);

  printf("[bq76] write_mem_u8: %02x = %02x [checksum: %02x]\n", reg_addr, reg_data, checksum);

  uint8_t buf = 0;
  bq76922_read_mem_u8(i2c, reg_addr, &buf);
}

int bq76922_read_mem_u16(i2c_inst_t* i2c, uint16_t reg_addr, uint16_t* reg_data) {
  bq76922_write_byte(i2c, 0x3e, reg_addr & 0xff);
  bq76922_write_byte(i2c, 0x3f, reg_addr >> 8);
  sleep_ms(10);

  *reg_data = bq76922_read_u16(i2c, 0x40);

  int len = bq76922_read_byte(i2c, 0x61);
  int checksum = bq76922_read_byte(i2c, 0x60);

  printf("[bq76] read_mem_u16: %02x = %04x [len: %d checksum: %02x]\n", reg_addr, *reg_data, len, checksum);

  return 1;
}

void bq76922_write_mem_i16(i2c_inst_t* i2c, uint16_t reg_addr, int16_t reg_data) {
  bq76922_write_byte(i2c, 0x3e, reg_addr & 0xff);
  bq76922_write_byte(i2c, 0x3f, reg_addr >> 8);
  bq76922_write_byte(i2c, 0x40, reg_data & 0xff);
  bq76922_write_byte(i2c, 0x41, reg_data >> 8);

  // 5 = len
  uint32_t checksum = (~((reg_addr & 0xff) + (reg_addr >> 8) + (reg_data & 0xff) + (reg_data >> 8))) & 0xff;
  bq76922_write_u16(i2c, 0x60, checksum | 6<<8);

  printf("[bq76] write_mem_i16: %02x = %04x [checksum: %02x]\n", reg_addr, reg_data, checksum);

  int16_t buf = 0;
  bq76922_read_mem_u16(i2c, reg_addr, (uint16_t*)&buf);
}

void bq76922_write_mem_u16(i2c_inst_t* i2c, uint16_t reg_addr, uint16_t reg_data) {
  bq76922_write_byte(i2c, 0x3e, reg_addr & 0xff);
  bq76922_write_byte(i2c, 0x3f, reg_addr >> 8);
  bq76922_write_byte(i2c, 0x40, reg_data & 0xff);
  bq76922_write_byte(i2c, 0x41, reg_data >> 8);

  // 5 = len
  uint32_t checksum = (~((reg_addr & 0xff) + (reg_addr >> 8) + (reg_data & 0xff) + (reg_data >> 8))) & 0xff;
  bq76922_write_u16(i2c, 0x60, checksum | 6<<8);

  printf("[bq76] write_mem_u16: %02x = %04x [checksum: %02x]\n", reg_addr, reg_data, checksum);

  uint16_t buf = 0;
  bq76922_read_mem_u16(i2c, reg_addr, &buf);
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

  // also interesting: https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1245177/bq76942-cell-balancing-not-activating

  uint16_t bal_active_cells = 0;
  uint16_t bal_status1 = 0;

  bq76922_read_mem_u16(i2c, 0x0083, &bal_active_cells);
  bq76922_read_mem_u16(i2c, 0x0085, &bal_status1);

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

  printf("[bq76] bal_active_cells: %016b\n", bal_active_cells);
  printf("[bq76] bal_status1: %d sec\n", bal_status1);

  // balance all cells above 3.4V
  bq76922_write_mem_u16(i2c, 0x0084, 3400);
  /*if (cell4_mv > 3400) {
    bq76922_write_mem_u16(i2c, 0x0083, 8);
  } else if (cell4_mv <= 3300) {
    bq76922_write_mem_u16(i2c, 0x0083, 0);
  }*/

  return 1;
}

void mon_all_fets_off(i2c_inst_t* i2c) {
  printf("[bq76] turning all fets off...\n");
  // ALL_FETS_OFF subcommand (0x0095)
  bq76922_write_byte(i2c, 0x3e, 0x95);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

// FIXME: call on cell overvoltage
void mon_discharge_fets_off(i2c_inst_t* i2c) {
  printf("[bq76] turning discharge fets off...\n");
  // CHG_PDSG_OFF subcommand (0x0094)
  bq76922_write_byte(i2c, 0x3e, 0x93);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

// FIXME: call on cell overvoltage
void mon_charge_fets_off(i2c_inst_t* i2c) {
  printf("[bq76] turning charge fets off...\n");
  // CHG_PCHG_OFF subcommand (0x0094)
  bq76922_write_byte(i2c, 0x3e, 0x94);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_all_fets_on(i2c_inst_t* i2c) {
  printf("[bq76] turning all fets on...\n");
  // ALL_FETS_ON subcommand (0x0096)
  bq76922_write_byte(i2c, 0x3e, 0x96);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_toggle_fet_en(i2c_inst_t* i2c) {
  printf("[bq76] fet_en toggle...\n");
  // FET_ENABLE subcommand (0x0022)
  // toggles the FET_EN bit in Manufacturing Status
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
  // TODO: investigate
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

void init_spi_client();

void turn_som_power_on() {
  init_spi_client();

  gpio_put(PIN_LED_B, 1);

  set_boot_magic();

  printf("# [action] turn_som_power_on\n");

  gpio_ext_enable(GPIO_EXT_3V3_EN);
  sleep_ms(10);
  gpio_ext_enable(GPIO_EXT_5V_EN);

  som_is_powered = true;
}

void turn_som_power_off() {
  init_spi_client();

  gpio_put(PIN_LED_B, 0);

  clear_boot_magic();

  printf("# [action] turn_som_power_off\n");

  gpio_ext_disable(GPIO_EXT_5V_EN);
  gpio_ext_disable(GPIO_EXT_3V3_EN);

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

  gpio_put(PIN_LED_R, 0);
  gpio_put(PIN_LED_G, 0);
  gpio_put(PIN_LED_B, 0);

  // FIXME this is now on (usb-c) gpio extender
  //gpio_init(PIN_USB_SRC_ENABLE);
  //gpio_set_dir(PIN_USB_SRC_ENABLE, 1);
  //gpio_put(PIN_USB_SRC_ENABLE, 0);

  // motherboard external GPIOS
  gpio_ext_setup();

  charger_configure();

  // if this is a warm boot, then we need to avoid latching the PWR and display
  // pins.
  if (syscon_warm_boot()) {
      printf("# [reset] watchdog scratch had valid on magic, not latching power.\n");
      som_is_powered = true;
  } else {
    // FIXME
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
  int input_mv = 0;

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
      else if (usb_c == 'c') {
        monitor_config_update(i2c0);
      }
      else if (usb_c == 'C') {
        monitor_config_update(i2c1);
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
              if (voltage > max_voltage && voltage <= 20) {
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

        // FIXME
        if (input_mv > 4000 && input_mv < 5100) {
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
      input_mv = charger_status();
      t_report = 0;
    }
  }

  return 0;
}
