/**
 * SPI commands from the SOM
 *
 * Ported from MNT Reform reform2-lpc-fw.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "spi_com.h"
#include "cli.h"
#include "machine.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

static struct cli_context spi_cli_ctx;

void init_spi_client(struct machine *mach) {
  spi_cli_ctx.mach = mach;

  gpio_set_function(PIN_SOM_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_SS0, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_SCK, GPIO_FUNC_SPI);

  // 4 MHz
  spi_init(spi1, 4000 * 1000);
  // we don't appreciate the wording, but it's the API we are given
  spi_set_slave(spi1, true);
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);

  cli_init(&spi_cli_ctx);

  printf("# [spi] init_spi_client done\n");
}

#define SPI_DEBUG_ENABLED 0
#define MAX_TXN_SZ 8*4

/* note that this runs in a timer interrupt:
   - no sleep_ms() calls
   - don't run longer than 4ms
 */
void handle_spi_commands(struct machine *machine) {
  if (!machine->som_is_powered) return;

  char rx_buf[MAX_TXN_SZ+1];
  memset(rx_buf, 0, MAX_TXN_SZ+1);

  int j = 0;
  int valid_c = 0;
#if SPI_DEBUG_ENABLED
  int raw_c = 0;
#endif
  while (spi_is_readable(spi1)) {
    j++;
    if (j >= MAX_TXN_SZ) break;
    uint8_t rx = (uint8_t)spi_get_hw(spi1)->dr;
    spi_get_hw(spi1)->dr = 0xff;

    // 0xb5 is a legacy command header, TBD if we should still support it
    if (rx != 0 && rx != 0xff) {
      rx_buf[valid_c++] = rx;
    }
  }

  uint64_t cli_err = 0;
  for (j = 0; j < valid_c; j++) {
    cli_char(&spi_cli_ctx, rx_buf[j]);
    int resp_len = cli_get_out_pos(&spi_cli_ctx);
    if (resp_len > 0) {
      // send the response
      int delayed_tot = 0;
      int delayed = 0;
      const char *cli_out_buf = cli_get_out(&spi_cli_ctx);
      for (int i = 0; i < resp_len; i++) {
        delayed = 0;
        while (!spi_is_writable(spi1)) {
          // wait up to 2ms for other side to receive
          busy_wait_us(100);
          delayed++;
          if (delayed > 20) break;
        }
        spi_get_hw(spi1)->dr = (uint32_t)cli_out_buf[i];
        // discard read
        [[maybe_unused]] uint8_t rx = (uint8_t)spi_get_hw(spi1)->dr;
        delayed_tot += delayed;
      }
#if SPI_DEBUG_ENABLED
      printf("# [spitx] %s d: %d\n", cli_out_buf, delayed_tot);
#endif
      cli_err = cli_get_err(&spi_cli_ctx);
      cli_reset_out(&spi_cli_ctx);
    }
  }

#if SPI_DEBUG_ENABLED
  if (raw_c > 0) {
    printf("# [spirx] ");
    for (int i = 0; i < raw_c; i++) {
      printf("%02x", rx_buf[i]);
      if (i >= valid_c) {
        printf("|");
      } else {
        printf(" ");
      }
      if (i % 8 == 7) printf("  ");
    }
    for (int i = 0; i < raw_c; i++) {
      if (isprint(rx_buf[i])) {
	printf("%c", rx_buf[i]);
      } else {
	printf(".");
      }
    }
    printf("\n");
  }
#endif

  if (cli_err) {
    char buf[5];
    printf("# cli err %llu %s\n", cli_err, fourcc_to_str(cli_err, buf));
    cli_reset(&spi_cli_ctx);
    cli_reset_out(&spi_cli_ctx);

    // drain RX
    while (spi_is_readable(spi1)) {
      [[maybe_unused]] uint8_t rx = (uint8_t)spi_get_hw(spi1)->dr;
    }

    // reset SPI0 block
    // this is a workaround for confusion with
    // software spi from BPI-CM4 where we get
    // bit-shifted bytes
    init_spi_client(machine);
    printf("\n");
  }
}
