#include <stdint.h>

#ifndef _POCKET_MP2650_H
#define _POCKET_MP2650_H

#define MPS_REG_INPUT_CURRENT_LIMIT_1 0x00
#define MPS_REGSTART_LIMITS MPS_REG_INPUT_CURRENT_LIMIT_1
#define MPS_REG_INPUT_VOLTAGE_LIMIT_1 0x01
#define MPS_REG_CHARGE_CURRENT 0x02

#define MPS_REG_CONFIG0 0x08
#define MPS_REGSTART_CONFIG MPS_REG_CONFIG0
#define MPS_REG_CONFIG1 0x09
#define MPS_REG_CONFIG2 0x0A
#define MPS_REG_CONFIG3 0x0B
#define MPS_REG_CONFIG4 0x0C

#define MPS_REG_STATUS 0x13
#define MPS_REGSTART_STATUS MPS_REG_STATUS
#define MPS_REG_FAULT 0x14

#define MPS_REG_ADC_BAT_V 0x16
#define MPS_REGSTART_ADC MPS_REG_ADC_BAT_V
#define MPS_REG_ADC_SYS_V 0x18
#define MPS_REG_ADC_BAT_CHARGE_I 0x1A
#define MPS_REG_ADC_INPUT_V 0x1C
#define MPS_REG_ADC_INPUT_I 0x1E
#define MPS_REG_ADC_OTG_OUTPUT_V 0x20
#define MPS_REG_ADC_OTG_OUTPUT_I 0x22
#define MPS_REG_ADC_JUNCTION_TEMP 0x24
#define MPS_REG_ADC_SYS_POWER 0x26
#define MPS_REG_ADC_BAT_DISCHARGE_I 0x28


typedef union mps_reg_limits_t {
  struct {
    uint8_t input_i_limit1;  // REG00H
    uint8_t input_v_limit;  // REG01H
    uint8_t charge_current;  // REG02H

    union {
      uint8_t reg_byte;
      struct {
        uint8_t i_term:4;
        uint8_t i_pre:4;
      };
    } reg03;  // REG03H

    union {
      uint8_t reg_byte;
      struct {
        uint8_t v_rech_os:1;
        uint8_t v_batt_reg:6;
        uint8_t :1;  // reserved
      };
    } reg04;

    union {
      uint8_t reg_byte;
      struct {
        uint8_t t_reg:2;
        uint8_t v_clamp:3;
        uint8_t r_batt:3;
      };
    } reg05;

    union {
      uint8_t reg_byte;
      struct {
        uint8_t v_in_otg_os:4;
        uint8_t v_in_otg:3;
        uint8_t :1;  // reserved
      };
    } reg06;

    union {
      uint8_t reg_byte;
      struct {
        uint8_t i_otg:4;
        uint8_t v_batt_pre:2;
        uint8_t :2;  // reserved
      };
    } reg07;
  };
  uint8_t all_regs[8];
} mps_reg_limits_t;

typedef union mps_reg_config_t {
  struct {
    union {
      uint8_t reg_byte;
      struct {
        uint8_t :1;  // reserved
        uint8_t battfet_en:1;
        uint8_t ntc_gcomp_sel:1;
        uint8_t susp_en:1;
        uint8_t chg_en:1;
        uint8_t otg_en:1;
        uint8_t wtd_rst:1;
        uint8_t reg_rst:1;
      };
    } config0;

    union {
      uint8_t reg_byte;
      struct {
        uint8_t tmr2x_en:1;
        uint8_t chg_tmr:2;
        uint8_t en_tmr:1;
        uint8_t wdt:2;
        uint8_t en_term:1;
        uint8_t :1;  // reserved
      };
    } config1;

    union {
      uint8_t reg_byte;
      struct {
        uint8_t ntc_cool:2;
        uint8_t ntc_warm:2;
        uint8_t ntc_ctrl:2;
        uint8_t jeita_vset:1;
        uint8_t jeita_iset:1;
      };
    } config2;

    union {
      uint8_t reg_byte;
      struct {
        uint8_t prochot_psys_cfg:2;
        uint8_t :1;  // reserved
        uint8_t sw_freq:2;
        uint8_t ibm_cfg:1;
        uint8_t :2;  // reserved
      };
    } config3;

    union {
      uint8_t reg_byte;
      struct {
        uint8_t independent_comparator_reference:1;
        uint8_t independent_comparator_cfg:1;
        uint8_t :1;  // reserved
        uint8_t virtual_diode_en:1;
        uint8_t vsys_prochottdb:1;
        uint8_t dschg_oc_prochot:3;
      };
    } config4;
  };
  uint8_t all_regs[5];
} mps_reg_config_t;

typedef union mps_reg_status_t {
  struct {
    union {
      uint8_t reg_byte;
      struct {
        uint8_t vsys_stat:1;
        uint8_t acok:1;
        uint8_t chg_stat:2; /* 0=Not charging, 1=Trickle/Pre, 2=Fast charging, 3=Charging done */
        uint8_t ppm_stat:1;
        uint8_t :1;
        uint8_t vsys_uv:1;
        uint8_t batt_uvlo:1;
      };
    } status; /* REG13h */

    union {
      uint8_t reg_byte;
      struct {
        uint8_t ntc_fault:3;
        uint8_t batt_fault:1;
        uint8_t chg_fault:2;
        uint8_t otg_fault:1;
        uint8_t watchdog_fault:1;
      };
    } fault; /* REG14h */
  };
  uint8_t all_regs[2];
} mps_reg_status_t;


typedef union mps_reg_adc_t {
  struct {
    uint16_t bat_v;  // 16-17  MSB 6400mV
    uint16_t sys_v;  // 18-19  MSB 6400mV
    uint16_t bat_charge_i;  // 1A-1B  MSB 6400mA
    uint16_t input_v;  // 1C-1D  MSB 12800mV
    uint16_t input_i;  // 1E-1F  MSB 3200mA
    uint16_t otg_output_v;  // 20-21
    uint16_t otg_output_i;  // 22-23
    uint16_t junction_t;  // 24-25
    uint16_t sys_p;  // 26-27
    uint16_t bat_discharge_i;  // 28-29
  };
  uint8_t all_regs[20];
} mps_reg_adc_t;

extern mps_reg_config_t mps_reg_config;
extern mps_reg_limits_t mps_reg_limits;
extern mps_reg_status_t mps_reg_status;
extern mps_reg_adc_t mps_reg_adc;

uint8_t mps_read_byte(uint8_t addr);

uint16_t mps_read_word(uint8_t addr);

float mps_word_to_ntc(uint16_t w);

uint16_t mps_word_to_3200(uint16_t w);
uint16_t mps_word_to_6400(uint16_t w);

uint16_t mps_word_to_12800(uint16_t w);

float mps_word_to_watt(uint16_t w);

float mps_word_to_temp(uint16_t w);

void mps_read_buf(uint8_t addr, uint8_t size, uint8_t *buf);

void mps_write_byte(uint8_t addr, uint8_t byte);

void mps_write_buf(uint8_t addr, uint8_t size, const uint8_t *buf);

#endif