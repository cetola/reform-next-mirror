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
#include "fusb302b.h"
#include "pd.h"

#define FW_STRING1 "NREF1SYS"
#define FW_STRING2 "R1"
#define FW_STRING3 "20240627"
#define FW_REV FW_STRING1 FW_STRING2 FW_STRING3

#define PIN_SDA 0
#define PIN_SCL 1

#define PIN_DISP_RESET 2
#define PIN_FLIGHTMODE 3
#define PIN_KBD_UART_TX 4
#define PIN_KBD_UART_RX 5
#define PIN_WOWWAN 6
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
#define PIN_MODEM_POWER 18
#define PIN_SOM_WAKE 19
#define PIN_MODEM_RESET 20
#define PIN_1V1_ENABLE 23
#define PIN_3V3_ENABLE 24
#define PIN_5V_ENABLE 25
#define PIN_PHONE_DPR 27
#define PIN_USB_SRC_ENABLE 28
#define PIN_PWREN_LATCH 29

// FUSB302B USB-PD controller
#define FUSB_ADDR 0x22
// MAX17320 protector/balancer
// https://datasheets.maximintegrated.com/en/ds/MAX17320.pdf
#define MAX_ADDR1 0x36
#define MAX_ADDR2 0x0b
// MP2650 charger
// https://www.monolithicpower.com/en/documentview/productdocument/index/version/2/document_type/Datasheet/lang/en/sku/MP2650GV/document_id/9664/
#define MPS_ADDR  0x5c

#define I2C_TIMEOUT (1000*500)

#define UART_ID uart1
#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

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

void i2c_scan() {
  printf("\nI2C Scan\n");
  printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

  for (int addr = 0; addr < (1 << 7); ++addr) {
    if (addr % 16 == 0) {
      printf("%02x ", addr);
    }

    int ret;
    uint8_t rxdata;
    ret = i2c_read_blocking(i2c0, addr, &rxdata, 1, false);

    printf(ret < 0 ? "." : "@");
    printf(addr % 16 == 15 ? "\n" : "  ");
  }
}

uint8_t fusb_read_byte(uint8_t addr)
{
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
  if (rxb!=0) printf("# [fusb] rx = 0x%02x\n", rxb);
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

int charger_configure() {
  // TODO
}

float charger_dump() {
  // TODO

  // carry over to globals for SPI reporting
  report_current = 0;
  report_volts = 0;

  return 0;
}

void init_spi_client();

void turn_som_power_on() {
  init_spi_client();

  // latch
  gpio_put(PIN_PWREN_LATCH, 1);

  gpio_put(PIN_LED_B, 1);

  printf("# [action] turn_som_power_on\n");
  gpio_put(PIN_1V1_ENABLE, 1);
  sleep_ms(10);
  gpio_put(PIN_3V3_ENABLE, 1);
  sleep_ms(10);

  /*gpio_put(PIN_SOM_MOSI, 1);
  gpio_put(PIN_SOM_SS0, 1);
  gpio_put(PIN_SOM_SCK, 1);
  gpio_put(PIN_SOM_MISO, 1);*/

  gpio_put(PIN_5V_ENABLE, 1);

  // MODEM
  gpio_put(PIN_FLIGHTMODE, 1); // active low
  gpio_put(PIN_MODEM_RESET, 0); // active low (?)
  gpio_put(PIN_MODEM_POWER, 1); // active high
  gpio_put(PIN_PHONE_DPR, 1); // active high

  sleep_ms(10);
  gpio_put(PIN_DISP_EN, 1);
  sleep_ms(10);
  gpio_put(PIN_DISP_RESET, 1);

  // MODEM
  gpio_put(PIN_MODEM_RESET, 1); // active low

  // done with latching
  gpio_put(PIN_PWREN_LATCH, 0);

  som_is_powered = true;
}

void turn_som_power_off() {
  init_spi_client();

  // latch
  gpio_put(PIN_PWREN_LATCH, 1);

  // FIXME spi test
  /*gpio_put(PIN_SOM_MOSI, 0);
  gpio_put(PIN_SOM_SS0, 0);
  gpio_put(PIN_SOM_SCK, 0);
  gpio_put(PIN_SOM_MISO, 0);*/

  gpio_put(PIN_LED_B, 0);

  printf("# [action] turn_som_power_off\n");
  gpio_put(PIN_DISP_RESET, 0);
  gpio_put(PIN_DISP_EN, 0);

  // MODEM
  gpio_put(PIN_FLIGHTMODE, 0); // active low
  gpio_put(PIN_MODEM_RESET, 0); // active low
  gpio_put(PIN_MODEM_POWER, 0); // active high
  gpio_put(PIN_PHONE_DPR, 0); // active high

  gpio_put(PIN_5V_ENABLE, 0);
  sleep_ms(10);
  gpio_put(PIN_3V3_ENABLE, 0);
  sleep_ms(10);
  gpio_put(PIN_1V1_ENABLE, 0);

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

  gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
  bi_decl(bi_2pins_with_func(PIN_SDA, PIN_SCL, GPIO_FUNC_I2C));
  i2c_init(i2c0, 100 * 1000);

  gpio_init(PIN_LED_R);
  gpio_init(PIN_LED_G);
  gpio_init(PIN_LED_B);
  gpio_set_dir(PIN_LED_R, 1);
  gpio_set_dir(PIN_LED_G, 1);
  gpio_set_dir(PIN_LED_B, 1);

  gpio_init(PIN_1V1_ENABLE);
  gpio_init(PIN_3V3_ENABLE);
  gpio_init(PIN_5V_ENABLE);
  gpio_set_dir(PIN_1V1_ENABLE, 1);
  gpio_set_dir(PIN_3V3_ENABLE, 1);
  gpio_set_dir(PIN_5V_ENABLE, 1);
  gpio_put(PIN_1V1_ENABLE, 0);
  gpio_put(PIN_3V3_ENABLE, 0);
  gpio_put(PIN_5V_ENABLE, 0);

  gpio_init(PIN_PWREN_LATCH);
  gpio_set_dir(PIN_PWREN_LATCH, 1);
  gpio_put(PIN_PWREN_LATCH, 0);

  gpio_init(PIN_DISP_RESET);
  gpio_init(PIN_DISP_EN);
  gpio_set_dir(PIN_DISP_EN, 1);
  gpio_set_dir(PIN_DISP_RESET, 1);
  gpio_put(PIN_DISP_EN, 0);
  gpio_put(PIN_DISP_RESET, 0);

  gpio_init(PIN_FLIGHTMODE);
  gpio_init(PIN_MODEM_POWER);
  gpio_init(PIN_MODEM_RESET);
  gpio_init(PIN_PHONE_DPR);
  gpio_set_dir(PIN_FLIGHTMODE, 1);
  gpio_set_dir(PIN_MODEM_POWER, 1);
  gpio_set_dir(PIN_MODEM_RESET, 1);
  gpio_set_dir(PIN_PHONE_DPR, 1);
  gpio_put(PIN_FLIGHTMODE, 0); // active low
  gpio_put(PIN_MODEM_POWER, 0); // active high
  gpio_put(PIN_MODEM_RESET, 0); // active low (?)
  gpio_put(PIN_PHONE_DPR, 0); // active high // causes 0.146W power use when high in off state!

  gpio_put(PIN_LED_R, 0);
  gpio_put(PIN_LED_G, 0);
  gpio_put(PIN_LED_B, 0);

  gpio_init(PIN_USB_SRC_ENABLE);
  gpio_set_dir(PIN_USB_SRC_ENABLE, 1);
  gpio_put(PIN_USB_SRC_ENABLE, 0);

  // latch the PWR and display pins
  gpio_put(PIN_PWREN_LATCH, 1);
  gpio_put(PIN_PWREN_LATCH, 0);

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

  /*while (1) {
    sleep_ms(1000);
    i2c_scan();
    }*/
  sleep_ms(5000);

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
    }
#endif

    if (state == 0) {
      gpio_put(PIN_LED_R, 0);
      power_objects = 0;
      request_sent = 0;

      // by default, we output 5V on VUSB
      gpio_put(PIN_USB_SRC_ENABLE, 1);

      //printf("# [pd] state 0\n");
      // probe FUSB302BMPX
      if (i2c_read_timeout_us(i2c0, FUSB_ADDR, rxdata, 1, false, I2C_TIMEOUT)) {
        // 1. set auto GoodCRC
        // AUTO_CRC in Switches1
        // Address: 03h; Reset Value: 0b0010_0000

        //printf("# [pd] FUSB probed.\n");

        fusb_write_byte(FUSB_RESET, FUSB_RESET_SW_RES);

        sleep_us(10);

        // turn on all power
        fusb_write_byte(FUSB_POWER, 0x0F);
        // automatic retransmission
        //fusb_write_byte(FUSB_CONTROL3, 0x07);
        // AUTO_HARDRESET | AUTO_SOFTRESET | 3 retries | AUTO_RETRY
        fusb_write_byte(FUSB_CONTROL3, (1<<4) | (1<<3) | (3<<1) | 1);
        // flush rx buffer
        fusb_write_byte(FUSB_CONTROL1, FUSB_CONTROL1_RX_FLUSH);

        // pdwn means pulldown. 0 = no pull down

        /* Measure CC1 */
        fusb_write_byte(FUSB_SWITCHES0, 4|2|1); //  MEAS_CC1|PDWN2   |PDWN1
        sleep_us(250);
        uint8_t cc1 = fusb_read_byte(FUSB_STATUS0) & FUSB_STATUS0_BC_LVL;

        //printf("# [pd] CC1: %d\n", cc1);

        /* Measure CC2 */
        fusb_write_byte(FUSB_SWITCHES0, 8|2|1); //  MEAS_CC2|PDWN2   |PDWN1
        sleep_us(250);
        uint8_t cc2 = fusb_read_byte(FUSB_STATUS0) & FUSB_STATUS0_BC_LVL;

        //printf("# [pd] CC2: %d\n", cc2);

        // detect orientation
        if (cc1 > cc2) {
          fusb_write_byte(FUSB_SWITCHES1,    4|1); //          |AUTO_CRC|TXCC1
          fusb_write_byte(FUSB_SWITCHES0,  4|2|1); //  MEAS_CC1|PDWN2   |PDWN1
        } else {
          fusb_write_byte(FUSB_SWITCHES1,    4|2); //          |AUTO_CRC|TXCC2
          fusb_write_byte(FUSB_SWITCHES0,  8|2|1); //  MEAS_CC2|PDWN2   |PDWN1
        }

        //printf("# [pd] switches set.\n");

        fusb_write_byte(FUSB_RESET, FUSB_RESET_PD_RESET);

        // automatic soft reset
        // FIXME disturbs PD with some supplies
        //fusb_write_byte(FUSB_CONTROL3, (1<<6) | (1<<4) | (1<<3) | (3<<1) | 1);
        //sleep_ms(1);
        //fusb_write_byte(FUSB_CONTROL3, (1<<4) | (1<<3) | (3<<1) | 1);

        //printf("# [pd] auto hard/soft reset and retries set.\n");

        t = 0;
        state = 1;
      } else {
        if (t > 1000) {
          printf("# [pd] state 0: fusb timeout.\n");
          t = 0;
        }
      }

    } else if (state == 1) {
      //printf("[next-sysctl] state 1\n");

      if (t>3000) {
        printf("# [pd] state 1, timeout.\n");
        float input_voltage = charger_dump();
        //max_dump();
        t = 0;

        // without batteries, the system dies here (brownout?)
        // but the charger might have set up the requested voltage anyway
        if (input_voltage < 6) {
          fusb_write_byte(FUSB_CONTROL3, (1<<6) | (1<<4) | (1<<3) | (3<<1) | 1);
          //sleep_ms(1);
          fusb_write_byte(FUSB_CONTROL3, (1<<4) | (1<<3) | (3<<1) | 1);
          state = 0;
        } else {
          state = 3;
        }
      }

      int res = fusb_read_message(&rx_msg);

      if (!res) {
        //printf("# [pd] s1: charger responds, turning off USB_SRC\n");
        // if a charger is responding, turn off our 5V output
        gpio_put(PIN_USB_SRC_ENABLE, 0);

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

      int current = 1;

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
      gpio_put(PIN_USB_SRC_ENABLE, 0);

      charger_configure();

      // charging
      sleep_ms(1);

      // running
      if (t>2000) {
        printf("# [pd] state 3.\n");

        float input_voltage = charger_dump();
        //max_dump();

        /*if (input_voltage < 6) {
          printf("# [pd] input voltage below 6v, renegotiate.\n");
          state = 0;
        }*/

        t = 0;
      }
    }

    t++;
    t_report++;
  }

  return 0;
}
