#ifndef _POCKET_UARTCOM_H
#define _POCKET_UARTCOM_H

#include <stdint.h>
#include "sysctl.h"

#define UART_BUFSZ 255
#define CMD_NUMBER_INVALID 0xffff

void handle_uart_commands(battery_info_s* battery_info);
void handle_commands(char chr, battery_info_s* battery_info);

typedef struct uart_state_s
{
    char remote_cmd;
    unsigned char cmd_state;
    unsigned int cmd_number;
    int echo;
} uart_state_s;


#endif
