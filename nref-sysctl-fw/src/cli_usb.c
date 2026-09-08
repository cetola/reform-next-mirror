/*
  SPDX-License-Identifier: GPL-3.0-or-later
  MNT Reform Next System Controller Firmware for RP2350
  Copyright 2026 MNT Research GmbH
*/

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "cli.h"
#include "cli_usb.h"

#define CLI_USB_STATE_NORMAL 0
#define CLI_USB_STATE_ESC 1
#define CLI_USB_STATE_CSI 2
#define CLI_USB_LINE_MAX 128
static int cli_usb_state = CLI_USB_STATE_NORMAL;
static char cli_usb_line_hist[CLI_USB_LINE_MAX];
static char cli_usb_line_temp[CLI_USB_LINE_MAX];
static char cli_usb_line[CLI_USB_LINE_MAX];
static int cli_usb_line_cursor = 0;
static struct cli_context cli_ctx_usb;

void cli_usb_line_del() {
  int rewind = 0;
  for (int i = cli_usb_line_cursor; i < CLI_USB_LINE_MAX-1; i++) {
    char c = cli_usb_line[i];
    char next = cli_usb_line[i + 1];
    if (c > 13 && next > 13) {
      cli_usb_line[i] = next;
      putchar(next);
      rewind++;
    } else {
      cli_usb_line[i] = 0;
      putchar(' ');
      rewind++;
      break;
    }
  }
  for (int i = 0; i < rewind; i++) {
    putchar(8);
  }
}

void cli_usb_line_reset() {
  for (int i = 0; i < cli_usb_line_cursor; i++) {
    putchar(8);
    putchar(' ');
    putchar(8);
  }
  cli_usb_line_cursor = 0;
}

void cli_usb_line_restore() {
  for (int i = 0; i < CLI_USB_LINE_MAX; i++) {
    if (cli_usb_line[i] < 13) break;
    putchar(cli_usb_line[i]);
    cli_usb_line_cursor++;
  }
}

void handle_usb_commands() {
  int usb_c = getchar_timeout_us(0);
  if (usb_c != PICO_ERROR_TIMEOUT) {

    if (cli_usb_state == CLI_USB_STATE_ESC) {
      //printf("esc: [0x%x]\n", usb_c);
      if (usb_c == '[') {
        cli_usb_state = CLI_USB_STATE_CSI;
        return;
      }
      if (usb_c >= 0x40 && usb_c <= 0x7e) {
        // "final byte"
        cli_usb_state = CLI_USB_STATE_NORMAL;
        return;
      }
      return;
    }
    if (cli_usb_state == CLI_USB_STATE_CSI) {
      //printf("csi: [0x%x]\n", usb_c);
      if (usb_c >= 0x40 && usb_c <= 0x7e) {
        // "final byte"
        if (usb_c == 0x44) {
          // cursor left
          if (cli_usb_line_cursor > 0) {
            putchar(8);
            cli_usb_line_cursor--;
          }
        } else if (usb_c == 0x43) {
          // cursor right
          if (cli_usb_line_cursor < CLI_USB_LINE_MAX-1) {
            if (cli_usb_line[cli_usb_line_cursor] > 13) {
              putchar(cli_usb_line[cli_usb_line_cursor]);
              cli_usb_line_cursor++;
            }
          }
        } else if (usb_c == 0x41 || usb_c == 0x42) {
          // cursor up or down
          cli_usb_line_reset();
          cli_usb_line_restore();
          cli_usb_line_reset();
          memcpy(cli_usb_line_temp, cli_usb_line, CLI_USB_LINE_MAX);
          memcpy(cli_usb_line, cli_usb_line_hist, CLI_USB_LINE_MAX);
          memcpy(cli_usb_line_hist, cli_usb_line_temp, CLI_USB_LINE_MAX);
          cli_usb_line_restore();
        } else if (usb_c == 0x7e) {
          // del
          cli_usb_line_del();
        }
        cli_usb_state = CLI_USB_STATE_NORMAL;
        return;
      }
      return;
    }
    if (usb_c == 0x1b) {
      // TODO: handle escape sequence
      cli_usb_state = CLI_USB_STATE_ESC;
      return;
    }

    if (usb_c == 13) usb_c = 10;
    if (usb_c == 0x7f || usb_c == 8) {
      // backspace
      if (cli_usb_line_cursor > 0) {
        putchar(8);
        putchar(' ');
        putchar(8);
        cli_usb_line_cursor--;
        cli_usb_line_del();
        //cli_usb_line[cli_usb_line_cursor] = 0;
      }
      return;
    }

    if (cli_usb_line_cursor < CLI_USB_LINE_MAX-1) {
      if (usb_c != 10) {
        char c = cli_usb_line[cli_usb_line_cursor];

        if (c > 13) {
          // in the middle of a line, insert and shift
          if (cli_usb_line[CLI_USB_LINE_MAX-2] == 0) {
            for (int i = CLI_USB_LINE_MAX - 2; i >= cli_usb_line_cursor; i--) {
              cli_usb_line[i + 1] = cli_usb_line[i];
            }
            int rewind = 0;
            for (int i = cli_usb_line_cursor; i < CLI_USB_LINE_MAX; i++) {
              char d = cli_usb_line[i];
              if (d > 13) {
                putchar(d);
                rewind++;
              }
            }
            for (int i = 0; i < rewind; i++) {
              putchar(8);
            }
            cli_usb_line[cli_usb_line_cursor] = usb_c;
            cli_usb_line_cursor++;
          }
        } else {
          // end of line
          cli_usb_line[cli_usb_line_cursor] = usb_c;
          cli_usb_line_cursor++;
        }
      }
      putchar(usb_c);
    }

    if (usb_c == 10) {
      for (int i=0; i<CLI_USB_LINE_MAX; i++) {
        // feed line into CLI
        if (cli_usb_line[i] == 0) {
          cli_usb_line[i] = 10;
        }

        cli_char(&cli_ctx_usb, cli_usb_line[i]);
        int resp_len = cli_get_out_pos(&cli_ctx_usb);
        if (resp_len > 0) {
          const char *cli_out_buf = cli_get_out(&cli_ctx_usb);
          printf("%s\n", cli_out_buf);
          cli_reset_out(&cli_ctx_usb);
        }

        if (cli_usb_line[i] == 0 || cli_usb_line[i] == 10) {
          // done
          cli_usb_line_cursor = 0;
          memcpy(cli_usb_line_hist, cli_usb_line, i);
          cli_usb_line_hist[i] = 0;
          memset(cli_usb_line, 0, CLI_USB_LINE_MAX);
          memset(cli_usb_line_temp, 0, CLI_USB_LINE_MAX);
          return;
        }
      }
    }
  }
}
