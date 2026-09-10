#ifndef _POCKET_SPICOM_H
#define _POCKET_SPICOM_H

#include "machine.h"

void init_spi_client(struct machine* mach);
void handle_spi_commands(struct machine *mach);

#endif
