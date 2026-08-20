#ifndef _POCKET_MAXCOM_H
#define _POCKET_MAXCOM_H

#include <stdint.h>
#include "sysctl.h"

uint8_t max_read_byte(uint8_t addr);
uint16_t max_read_word(uint8_t addr);
uint16_t max_read_word_100(uint8_t addr);
void max_read_buf(uint8_t addr, uint8_t size, uint8_t *buf);
void max_write_byte(uint8_t addr, uint8_t byte);
void max_write_word(uint8_t addr, uint16_t word);
void max_write_word_100(uint8_t addr, uint16_t word);
void max_write_buf(uint8_t addr, uint8_t size, const uint8_t *buf);
float max_word_to_mv(uint16_t w);
float max_word_to_pack_mv(uint16_t w);
float max_word_to_ma(uint16_t w);
float max_word_to_time(uint16_t w);
float max_word_to_cap(uint16_t w);
float max_word_to_percentage(uint16_t w);

#endif