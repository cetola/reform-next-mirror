#ifndef _BQ25792_H
#define _BQ25792_H

#include <stdint.h>
#include "next_pack.h"

uint8_t bq25792_read_byte(uint8_t addr);
int16_t bq25792_read_word_signed(uint8_t addr);
uint16_t bq25792_read_word(uint8_t addr);
void bq25792_write_byte(uint8_t addr, uint8_t byte);
void bq25792_write_word(uint8_t addr, uint16_t word);
void bq25792_write_word_signed(uint8_t addr, int16_t word);
void charger_init();
void charger_configure();
void charger_set_input_current(int ma);
void charger_set_charge_current(int ma);
int charger_status(struct BatteryPack* packs);
void bq25792_set_debug(int debug);

#endif
