
// 1 coulomb = 1 amp * second
struct BatteryPack {
  int id;
  i2c_inst_t* i2c;
  bool active;
  float volt;
  float ampere;
  float cells_v[8];
  int overvolt;
  int undervolt;
  int fully_charged;
  float coulomb_max;
  float coulomb_cur;
  float gauge_percent;
  float temp_int_k;
  float temp_ext_k;
  int bal_active_cells;
};

