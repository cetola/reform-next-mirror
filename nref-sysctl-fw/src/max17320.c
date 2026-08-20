#include "max17320.h"

uint8_t max_read_byte(uint8_t addr)
{
  uint8_t buf;
  i2c_write_blocking(i2c0, MAX_ADDR1, &addr, 1, true);
  i2c_read_blocking(i2c0, MAX_ADDR1, &buf, 1, false);
  return buf;
}

uint16_t max_read_word(uint8_t addr)
{
  uint8_t buf[2];
  i2c_write_blocking(i2c0, MAX_ADDR1, &addr, 1, true);
  i2c_read_blocking(i2c0, MAX_ADDR1, buf, 2, false);
  uint16_t result = ((uint16_t)buf[1]<<8) | (uint16_t)buf[0];
  return result;
}

uint16_t max_read_word_100(uint8_t addr)
{
  uint8_t buf[2];
  i2c_write_blocking(i2c0, MAX_ADDR2, &addr, 1, true);
  i2c_read_blocking(i2c0, MAX_ADDR2, buf, 2, false);
  uint16_t result = ((uint16_t)buf[1]<<8) | (uint16_t)buf[0];
  return result;
}

void max_read_buf(uint8_t addr, uint8_t size, uint8_t *buf)
{
  i2c_write_blocking(i2c0, MAX_ADDR1, &addr, 1, true);
  i2c_read_blocking(i2c0, MAX_ADDR1, buf, size, false);
}

void max_write_byte(uint8_t addr, uint8_t byte)
{
  uint8_t buf[2] = {addr, byte};
  i2c_write_blocking(i2c0, MAX_ADDR1, buf, 2, false);
}

void max_write_word(uint8_t addr, uint16_t word)
{
  uint8_t buf[3] = {addr, word&0xff, word>>8};
  i2c_write_blocking(i2c0, MAX_ADDR1, buf, 3, false);
}

void max_write_word_100(uint8_t addr, uint16_t word)
{
  uint8_t buf[3] = {addr, word&0xff, word>>8};
  i2c_write_blocking(i2c0, MAX_ADDR2, buf, 3, false);
}

void max_write_buf(uint8_t addr, uint8_t size, const uint8_t *buf)
{
  uint8_t txbuf[size + 1];
  txbuf[0] = addr;
  for (int i = 0; i < size; i++) {
    txbuf[i + 1] = buf[i];
  }
  i2c_write_blocking(i2c0, MAX_ADDR1, txbuf, size + 1, false);
}

float max_word_to_mv(uint16_t w)
{
  float result = ((float)w)*0.078125;
  return result;
}

float max_word_to_pack_mv(uint16_t w)
{
  float result = ((float)w)*0.3125;
  return result;
}

float max_word_to_ma(uint16_t w)
{
  float result = ((float)w)*0.3125;
  return result;
}

float max_word_to_time(uint16_t w)
{
  float result = ((float)w)*5.625;
  return result;
}

float max_word_to_cap(uint16_t w)
{
  float result = ((float)w)*1.0; // depends on Rsense, 1.0 @ 5mohms. otherwise 5.0μVh / Rsense
  return result;
}

float max_word_to_percentage(uint16_t w)
{
  // TODO: cap to 100%
  float result = ((float)w)*0.00390625;
  return result;
}