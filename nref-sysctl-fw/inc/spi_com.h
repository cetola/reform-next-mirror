#ifndef _POCKET_SPICOM_H
#define _POCKET_SPICOM_H

#include <stdint.h>
#include "machine.h"

void init_spi_client();
void handle_spi_commands(struct machine *mach);

#endif
