/* MNT Reform Next Battery Pack */

#ifndef _NEXT_PACK_H
#define _NEXT_PACK_H

#include "hardware/i2c.h"

// battery information
#define MV_OVERVOLT 3800
#define MV_UNDERVOLT 2450
// some cells don't hold voltage > 3.5
#define MV_FULL 3500
#define MV_BALANCE_ABOVE 3600
#define MV_HYST 200

struct battery_pack {
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
  float ms_at_rest;
  float time_last_ms;
  // 1 coulomb = 1 amp * second
  float coulomb_max; // the design capacity (upper end)
  float coulomb_cur; // the current estimate
  float coulomb_zero; // the point where we actually hit low voltage (0%)
  float gauge_percent;
  float temp_int_k;
  float temp_ext_k;
  int bal_active_cells;
};

struct machine;

int battery_pack_task(struct machine *mach, struct battery_pack *pack, float ms_elapsed);
void battery_pack_setup(i2c_inst_t* i2c);

#endif
