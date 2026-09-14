/* MNT Reform Next Charger (on Mainboard) */

#ifndef _NEXT_CHARGER_H
#define _NEXT_CHARGER_H

#include "machine_next.h"

void charger_init(struct machine* mach);
void charger_task(struct machine* mach);
void charger_set_input_current(struct machine* mach, int ma);
void charger_set_charge_current(struct machine* mach, int ma);
int charger_status(struct machine* mach);
int charger_get_total_capacity_mah(struct machine* mach);

#endif
