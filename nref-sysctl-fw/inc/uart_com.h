#ifndef _POCKET_UARTCOM_H
#define _POCKET_UARTCOM_H

#include "machine.h"

void uart_com_init(struct machine* mach);
void handle_uart_commands();

#endif
