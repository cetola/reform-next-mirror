#include "hardware/i2c.h"
#include "sysctl.h"
#include "mp2650.h"

mps_reg_config_t mps_reg_config;
mps_reg_limits_t mps_reg_limits;
mps_reg_status_t mps_reg_status;
mps_reg_adc_t mps_reg_adc;

uint8_t mps_read_byte(uint8_t addr)
{
  uint8_t buf;
  i2c_write_blocking(i2c0, MPS_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, MPS_ADDR, &buf, 1, false);
  return buf;
}

uint16_t mps_read_word(uint8_t addr)
{
  uint8_t buf[2];
  i2c_write_blocking(i2c0, MPS_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, MPS_ADDR, buf, 2, false);
  uint16_t result = ((uint16_t)buf[1]<<8) | (uint16_t)buf[0];
  return result;
}

float mps_word_to_ntc(uint16_t w)
{
  float result = (float)(w&0xfff)*1.6/4096.0;
  return result;
}

uint16_t mps_word_to_12800(uint16_t w)
{
  return (w>>6) * 25;
}

uint16_t mps_word_to_3200(uint16_t w)
{
  uint16_t result = (w>>8) * 25;
  if (w & 0x80) {
    result += 12;  // should be 12.5.
  }
  if (w & 0x40) {
    result += 6;  // should be 6.25.
  }
  return result;
}

uint16_t mps_word_to_6400(uint16_t w)
{
  uint16_t result = (w>>7) * 25;
  if (w & 0x40) {
    result += 12;  // should be 12.5.
  }
  return result;
}

// range: 127.875 to 0.125
float mps_word_to_watt(uint16_t w)
{
  return (float)(w >> 6) / (float)8;
}

float mps_word_to_temp(uint16_t w)
{
  // tj=903-2.578*t
  // tj-903=-2.578*t
  // (tj-903)/-2.578=t

  float result = (float)(w>>6);
  result = (result - 903) / -2.578;
  //result = 903-2.578*result;
  return result;
}

void mps_read_buf(uint8_t addr, uint8_t size, uint8_t *buf)
{
  i2c_write_blocking(i2c0, MPS_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, MPS_ADDR, buf, size, false);
}

void mps_write_byte(uint8_t addr, uint8_t byte)
{
  uint8_t buf[2] = {addr, byte};
  i2c_write_blocking(i2c0, MPS_ADDR, buf, 2, false);
}

void mps_write_buf(uint8_t addr, uint8_t size, const uint8_t *buf)
{
  uint8_t txbuf[size + 1];
  txbuf[0] = addr;
  for (int i = 0; i < size; i++) {
    txbuf[i + 1] = buf[i];
  }
  i2c_write_blocking(i2c0, MPS_ADDR, txbuf, size + 1, false);
}
