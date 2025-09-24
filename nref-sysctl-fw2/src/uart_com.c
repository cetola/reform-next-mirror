#include <stdio.h>
#include <string.h>
#include "hardware/uart.h"
#include "uart_com.h"
#include "pd_com.h"

void handle_uart_commands(battery_info_s* battery_info)
{
  // prevent endless loop
  int uart_max = 8;
  while (uart_is_readable(UART_ID) && uart_max > 0)
  {
    handle_commands(uart_getc(UART_ID), battery_info);
    uart_max--;
  }
}

/**
 * UART commands from the keyboard
 */
void handle_commands(char chr, battery_info_s* battery_info)
{
    static uart_state_s uart_state = {0};
    struct BatteryPack* packs = battery_info->packs;

    char uart_buffer[UART_BUFSZ+1] = {0};

    if (uart_state.echo)
    {
        snprintf(uart_buffer, UART_BUFSZ, "%c", chr);
        uart_puts(UART_ID, uart_buffer);
    }

    // states:
    // 0-3 digits of optional command argument
    // 4   command letter expected
    // 5   syntax error (unexpected character)
    // 6   command letter entered

    if (uart_state.cmd_state <= ST_EXPECT_DIGIT_3)
    {
        // read number or command
        if (chr >= '0' && chr <= '9')
        {
            if (uart_state.cmd_number == CMD_NUMBER_INVALID)
            {
                uart_state.cmd_number = 0;
            }
            uart_state.cmd_number *= 10;
            uart_state.cmd_number += (chr - '0');
            uart_state.cmd_state++;
        }
        else if ((chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z'))
        {
            // command entered instead of digit
            uart_state.remote_cmd = chr;
            uart_state.cmd_state = ST_EXPECT_RETURN;
        }
        else if (chr == '\n' || chr == ' ')
        {
            // ignore newlines or spaces
        }
        else if (chr == '\r')
        {
            snprintf(uart_buffer, UART_BUFSZ, "error:syntax\r\n");
            uart_puts(UART_ID, uart_buffer);
            uart_state.cmd_state = ST_EXPECT_DIGIT_0;
            uart_state.cmd_number = CMD_NUMBER_INVALID;
        }
        else
        {
            // syntax error
            uart_state.cmd_state = ST_SYNTAX_ERROR;
        }
    }
    else if (uart_state.cmd_state == ST_EXPECT_CMD)
    {
        // read command
        if ((chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z'))
        {
            uart_state.remote_cmd = chr;
            uart_state.cmd_state = ST_EXPECT_RETURN;
        }
        else
        {
            uart_state.cmd_state = ST_SYNTAX_ERROR;
        }
    }
    else if (uart_state.cmd_state == ST_SYNTAX_ERROR)
    {
        // syntax error
        if (chr == '\r')
        {
            printf("# [keyboard] [ERROR] syntax error\n");
            snprintf(uart_buffer, UART_BUFSZ, "error:syntax\r\n");
            uart_puts(UART_ID, uart_buffer);
            uart_state.cmd_state = ST_EXPECT_DIGIT_0;
            uart_state.cmd_number = CMD_NUMBER_INVALID;
        }
    }
    else if (uart_state.cmd_state == ST_EXPECT_RETURN)
    {
        if (chr == '\n' || chr == ' ')
        {
            // ignore newlines or spaces
        }
        else if (chr == '\r')
        {
            printf("# [keyboard] exec: %c %d\n", uart_state.remote_cmd, uart_state.cmd_number);
            if (uart_state.echo)
            {
                // FIXME
                snprintf(uart_buffer, UART_BUFSZ, "\n");
                uart_puts(UART_ID, uart_buffer);
            }

            // execute
            if (uart_state.remote_cmd == 'p')
            {
                // toggle system power and/or reset imx
                if (uart_state.cmd_number == 0)
                {
                    turn_som_power_off();
                    snprintf(uart_buffer, UART_BUFSZ, "system: off\r\n");
                    uart_puts(UART_ID, uart_buffer);
                }
                else if (uart_state.cmd_number == 2)
                {
                    // reset_som();
                    snprintf(uart_buffer, UART_BUFSZ, "system: reset\r\n");
                    uart_puts(UART_ID, uart_buffer);
                }
                else if (uart_state.cmd_number == 1)
                {
                    turn_som_power_on();
                    snprintf(uart_buffer, UART_BUFSZ, "system: on\r\n");
                    uart_puts(UART_ID, uart_buffer);
                }
            }
            else if (uart_state.remote_cmd == 'a')
            {
                // TODO
                // get system current (mA)
                snprintf(uart_buffer, UART_BUFSZ, "%d\r\n", 0);
                uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 'v')
            {
                // TODO
                // get cell voltage
                snprintf(uart_buffer, UART_BUFSZ, "%d\r\n", 0);
                uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 'V')
            {
                // TODO
                // get system voltage
                snprintf(uart_buffer, UART_BUFSZ, "%d\r\n", 0);
                uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 's')
            {
                char tmp[9];
                strlcpy(tmp, MNTRE_FIRMWARE_VERSION, sizeof(tmp));
                snprintf(uart_buffer, UART_BUFSZ, "MNT Reform Next    " FW_STRING1 FW_STRING2 "%s\r\n", MNTRE_FIRMWARE_VERSION);
                uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 'u')
            {
                // TODO
                // turn reporting to i.MX on or off
            }
            else if (uart_state.remote_cmd == 'w')
            {
                // wake SoC
                som_wake();
                snprintf(uart_buffer, UART_BUFSZ, "system: wake\r\n");
                uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 'c')
            {
              // get status of cells, current, voltage, fuel gauge
              int mA = (int)(packs[0].ampere*1000.0 + packs[1].ampere*1000.0);
              float gauge_percent = 0.0;
              float mV = 0.0;
              float num_packs = 0;
              // FIXME DUPLICATION
              if (packs[0].active) {
                gauge_percent += packs[0].gauge_percent;
                mV += packs[0].volt * 1000.0;
                num_packs++;
              }
              if (packs[1].active) {
                gauge_percent += packs[1].gauge_percent;
                mV += packs[1].volt * 1000.0;
                num_packs++;
              }
              if (num_packs >= 2) {
                gauge_percent /= num_packs;
                mV /= num_packs;
              }

              char mA_sign = ' ';
              if (mA<0) {
                mA = -mA;
                mA_sign = '-';
              }
              sprintf(uart_buffer,"%02d %02d %02d %02d %02d %02d %02d %02d mA%c%04dmV%05d %3d%% P%d\r\n",
                      (int)(packs[0].cells_v[0]/100),
                      (int)(packs[0].cells_v[1]/100),
                      (int)(packs[0].cells_v[2]/100),
                      (int)(packs[0].cells_v[3]/100),
                      (int)(packs[1].cells_v[0]/100),
                      (int)(packs[1].cells_v[1]/100),
                      (int)(packs[1].cells_v[2]/100),
                      (int)(packs[1].cells_v[3]/100),
                      mA_sign,
                      mA,
                      (int)mV,
                      (int)gauge_percent,
                      battery_info->som_is_powered?1:0);

              //printf("[uart] %s", uart_buffer);
              uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 'S')
            {
                // TODO
                // get charger system cycles in current state
                snprintf(uart_buffer, UART_BUFSZ, "%d\r\n", 0);
                uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 'C')
            {
                // TODO
                // get battery capacity (mAh)
                snprintf(uart_buffer, UART_BUFSZ, "%d/%d/%d\r\n", 0, 0, 0);
                uart_puts(UART_ID, uart_buffer);
            }
            else if (uart_state.remote_cmd == 'e')
            {
                // toggle serial echo
                uart_state.echo = uart_state.cmd_number == 1 ? 1 : 0;
            }
            else
            {
                snprintf(uart_buffer, UART_BUFSZ, "error:command\r\n");
                uart_puts(UART_ID, uart_buffer);
            }

            uart_state.cmd_state = ST_EXPECT_DIGIT_0;
            uart_state.cmd_number = CMD_NUMBER_INVALID;
        }
        else
        {
            uart_state.cmd_state = ST_SYNTAX_ERROR;
        }
    }
}
