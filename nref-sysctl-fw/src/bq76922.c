/* BQ76922 is the monitor and balancer chip on each battery pack of MNT Reform Next. */

#include <bq76922.h>

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define BQ76922_ADDR 0x08

int bq76922_detect(i2c_inst_t* i2c) {
  uint8_t addr = 0x00;
  uint8_t buf = 0x00;
  int res = i2c_write_blocking(i2c, BQ76922_ADDR, &addr, 1, true);
  if (res == PICO_ERROR_GENERIC) return res;
  res = i2c_read_blocking(i2c, BQ76922_ADDR, &buf, 1, false);
  return res;
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

  // FIXME: really 10ms needed?
  busy_wait_us(10*1000);
  *reg_data = bq76922_read_byte(i2c, 0x40);

  //int len = bq76922_read_byte(i2c, 0x61);
  //int checksum = bq76922_read_byte(i2c, 0x60);
  //printf("[bq76] read_mem_u8: %02x = %02x [len: %d checksum: %02x]\n", reg_addr, *reg_data, len, checksum);

  return 1;
}

void bq76922_write_mem_u8(i2c_inst_t* i2c, uint16_t reg_addr, uint8_t reg_data) {
  bq76922_write_byte(i2c, 0x3e, reg_addr & 0xff);
  bq76922_write_byte(i2c, 0x3f, reg_addr >> 8);

  bq76922_write_byte(i2c, 0x40, reg_data);

  // 5 = len
  uint32_t checksum = (~((reg_addr & 0xff) + (reg_addr >> 8) + reg_data)) & 0xff;
  bq76922_write_u16(i2c, 0x60, checksum | 5<<8);

  //printf("[bq76] write_mem_u8: %02x = %02x [checksum: %02x]\n", reg_addr, reg_data, checksum);

  uint8_t buf = 0;
  bq76922_read_mem_u8(i2c, reg_addr, &buf);

  if (buf != reg_data) {
    printf("# [bq76] WARN: reg 0x%04x wrote: %02x read: %02x\n", reg_addr, reg_data, buf);
  }
}

int bq76922_read_mem_u16(i2c_inst_t* i2c, uint16_t reg_addr, uint16_t* reg_data) {
  bq76922_write_byte(i2c, 0x3e, reg_addr & 0xff);
  bq76922_write_byte(i2c, 0x3f, reg_addr >> 8);

  // FIXME
  busy_wait_us(10*1000);
  *reg_data = bq76922_read_u16(i2c, 0x40);

  //int len = bq76922_read_byte(i2c, 0x61);
  //int checksum = bq76922_read_byte(i2c, 0x60);
  //printf("[bq76] read_mem_u16: %02x = %04x [len: %d checksum: %02x]\n", reg_addr, *reg_data, len, checksum);

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

  //printf("[bq76] write_mem_i16: %02x = %04x [checksum: %02x]\n", reg_addr, reg_data, checksum);

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

  //printf("[bq76] write_mem_u16: %02x = %04x [checksum: %02x]\n", reg_addr, reg_data, checksum);

  uint16_t buf = 0;
  bq76922_read_mem_u16(i2c, reg_addr, &buf);
}

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
    // FIXME
    busy_wait_us(10*1000);
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

void mon_all_fets_off(i2c_inst_t* i2c) {
  printf("[bq76] turning all fets off...\n");
  // ALL_FETS_OFF subcommand (0x0095)
  bq76922_write_byte(i2c, 0x3e, 0x95);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_discharge_fets_off(i2c_inst_t* i2c) {
  printf("[bq76] turning discharge fets off...\n");
  // CHG_PDSG_OFF subcommand (0x0094)
  bq76922_write_byte(i2c, 0x3e, 0x93);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_charge_fets_off(i2c_inst_t* i2c) {
  printf("[bq76] turning charge fets off...\n");
  // CHG_PCHG_OFF subcommand (0x0094)
  bq76922_write_byte(i2c, 0x3e, 0x94);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_all_fets_on(i2c_inst_t* i2c) {
  //printf("[bq76] turning all fets on...\n");
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

void mon_toggle_pf_en(i2c_inst_t* i2c) {
  printf("[bq76] pf_en toggle...\n");
  // FET_ENABLE subcommand (0x0024)
  // toggles the PF_EN bit in Manufacturing Status
  bq76922_write_byte(i2c, 0x3e, 0x24);
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
  //printf("[bq76] turning sleep off...\n");
  bq76922_write_byte(i2c, 0x3e, 0x9a);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}

void mon_sleep_on(i2c_inst_t* i2c) {
  //printf("[bq76] turning sleep on...\n");
  bq76922_write_byte(i2c, 0x3e, 0x99);
  bq76922_write_byte(i2c, 0x3f, 0x00);
}
