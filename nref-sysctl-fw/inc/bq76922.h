/* BQ76922 is the monitor and balancer chip on each battery pack of MNT Reform Next. */

#ifndef _BQ76922_H
#define _BQ76922_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

int bq76922_detect(i2c_inst_t* i2c);
uint8_t bq76922_read_byte(i2c_inst_t* i2c, uint8_t addr);
uint16_t bq76922_read_u16(i2c_inst_t* i2c, uint8_t addr);
void bq76922_write_byte(i2c_inst_t* i2c, uint8_t addr, uint8_t byte);
void bq76922_write_i16(i2c_inst_t* i2c, uint8_t addr, int16_t word);
void bq76922_write_u16(i2c_inst_t* i2c, uint8_t addr, uint16_t word);
int bq76922_read_mem_u8(i2c_inst_t* i2c, uint16_t reg_addr, uint8_t* reg_data);
void bq76922_write_mem_u8(i2c_inst_t* i2c, uint16_t reg_addr, uint8_t reg_data);
int bq76922_read_mem_u16(i2c_inst_t* i2c, uint16_t reg_addr, uint16_t* reg_data);
void bq76922_write_mem_i16(i2c_inst_t* i2c, uint16_t reg_addr, int16_t reg_data);
void bq76922_write_mem_u16(i2c_inst_t* i2c, uint16_t reg_addr, uint16_t reg_data);

int mon_read_subcommand(i2c_inst_t* i2c, uint8_t subcmd, uint8_t* buf, int len);
void mon_all_fets_off(i2c_inst_t* i2c);
void mon_discharge_fets_off(i2c_inst_t* i2c);
void mon_charge_fets_off(i2c_inst_t* i2c);
void mon_all_fets_on(i2c_inst_t* i2c);
void mon_toggle_fet_en(i2c_inst_t* i2c);
void mon_toggle_pf_en(i2c_inst_t* i2c);
void mon_fet_test(i2c_inst_t* i2c);
void mon_sleep_off(i2c_inst_t* i2c);
void mon_sleep_on(i2c_inst_t* i2c);

#endif
