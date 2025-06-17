/* MNT Reform Next Battery Pack */

#ifndef _NEXT_PACK_H
#define _NEXT_PACK_H

#include "hardware/i2c.h"

// battery information
// 2.0A x 3600 seconds/hour (pack capacity)
#define MAX_CAPACITY (2.0 * 3600.0)
#define MV_OVERVOLT 3800
#define MV_UNDERVOLT 2450
#define MV_FULL 3500 // some cells don't hold voltage > 3.5
#define MV_BALANCE_ABOVE 3600
#define MV_HYST 200

struct BatteryPack {
  int id;
  i2c_inst_t* i2c;
  bool active;
  bool debug;
  float volt;
  float ampere;
  float cells_v[8];
  int overvolt;
  int undervolt;
  int fully_charged;
  // 1 coulomb = 1 amp * second
  float coulomb_max;
  float coulomb_cur;
  float gauge_percent;
  float temp_int_k;
  float temp_ext_k;
  int bal_active_cells;
};

int pack_configure(struct BatteryPack* pack, float ms_elapsed);
void monitor_config_update(i2c_inst_t* i2c);
void monitor_setup(i2c_inst_t* i2c);

#endif
