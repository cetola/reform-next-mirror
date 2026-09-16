#include "uart_com.h"
#include "cli.h"
#include "machine.h"
#include "hardware/uart.h"
#include <stdio.h>

static struct cli_context uart_cli_ctx;

void uart_com_init(struct machine *mach) {
  uart_cli_ctx.mach = mach;
  cli_init(&uart_cli_ctx);
}

void handle_uart_commands() {
  // prevent endless loop
  int uart_max = 32;
  uint64_t cli_err = 0;
  while (uart_is_readable(UART_ID) && uart_max > 0) {
    char c = uart_getc(UART_ID);
    // substitute \r for \n
    if (c == 13) c = 10;
    cli_char(&uart_cli_ctx, c);
    if (cli_get_out_pos(&uart_cli_ctx)) {
      uart_puts(UART_ID, cli_get_out(&uart_cli_ctx));
      printf("# [kbd<] %s\n", cli_get_out(&uart_cli_ctx));
      cli_err = cli_get_err(&uart_cli_ctx);
      cli_reset_out(&uart_cli_ctx);
    }
    uart_max--;
  }
  if (cli_err) {
    printf("# cli err %llu\n", cli_err);
    cli_reset(&uart_cli_ctx);
    cli_reset_out(&uart_cli_ctx);
  }
}
