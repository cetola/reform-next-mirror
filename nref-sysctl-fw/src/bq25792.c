#include <stdint.h>
#include <stdio.h>
#include "hardware/i2c.h"
#include "next_pack.h"
#include "bq25792.h"
#include "sysctl.h"

// BQ25792 charger
#define BQ25792_ADDR 0x6b

uint8_t bq25792_read_byte(uint8_t addr) {
  uint8_t buf;
  i2c_write_blocking(i2c0, BQ25792_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, BQ25792_ADDR, &buf, 1, false);
  return buf;
}

int16_t bq25792_read_word_signed(uint8_t addr) {
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

uint16_t bq25792_read_word(uint8_t addr) {
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

void bq25792_write_byte(uint8_t addr, uint8_t byte) {
  uint8_t buf[2] = {addr, byte};
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf, 2, false);
}

void bq25792_write_word(uint8_t addr, uint16_t word) {
  uint8_t buf_msb[2] = {addr, word>>8};
  uint8_t buf_lsb[2] = {addr+1, word & 0xff};

  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_msb, 2, false);
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_lsb, 2, false);
}

void bq25792_write_word_signed(uint8_t addr, int16_t word) {
  uint8_t buf_msb[2] = {addr, word>>8};
  uint8_t buf_lsb[2] = {addr+1, word & 0xff};

  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_msb, 2, false);
  i2c_write_blocking(i2c0, BQ25792_ADDR, buf_lsb, 2, false);
}
