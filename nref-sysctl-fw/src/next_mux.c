#include <stdio.h>
#include "hardware/gpio.h"
#include "next_mux.h"
#include "next_gpio.h"
#include "machine.h"

void mux_set_pd_dir(bool dir) {
  printf("setting USB-C mux dir to %d...\n", dir);
  if (dir) {
    gpio_ext_pd_enable(2);
  } else {
    gpio_ext_pd_disable(2);
  }
}

void mux_set_usb_mode([[maybe_unused]] int port, int mode) {
  // Reform Next has only one muxable port
  switch (mode) {
  case 0: {
    // configuration for SysCtl flashing (default)
    printf("setting USWITCH_1 to 0...\n");
    gpio_mb_disable(4);
    printf("setting USWITCH_2 to 0...\n");
    gpio_mb_disable(5);
    printf("setting USWITCH_3 to 0...\n");
    gpio_mb_disable(6);
    break;
  }
  case 1: {
    // configuration for USB-UART (SoC console), and EDL providing USB hub upstream
    // sysctl reachable via SoC USB
    printf("setting USWITCH_1 to 1...\n");
    gpio_mb_enable(4);
    printf("setting USWITCH_2 to 1...\n");
    gpio_mb_enable(5);
    printf("setting USWITCH_3 to 1...\n");
    gpio_mb_enable(6);

    // turn off sending to SoC UART
    gpio_init(PIN_SOM_UART_TX);
    gpio_set_dir(PIN_SOM_UART_TX, 0);
    break;
  }
  case 2: {
    // configuration for EDL port on USB-C
    // no going back from this via USB, sysctl unreachable except for SPI
    printf("setting USWITCH_1 to 1...\n");
    gpio_mb_enable(4);
    printf("setting USWITCH_2 to 0...\n");
    gpio_mb_disable(5);
    printf("setting USWITCH_3 to 0...\n");
    gpio_mb_disable(6);
    break;
  }
  case 3: {
    // configuration for normal USB-C and SysCtl as USB device of SoC
    // sysctl reachable via SoC USB
    printf("setting USWITCH_1 to 1...\n");
    gpio_mb_enable(4);
    printf("setting USWITCH_2 to 0...\n");
    gpio_mb_disable(5);
    printf("setting USWITCH_3 to 1...\n");
    gpio_mb_enable(6);
    break;
  }
  default: {
  }
  }
}
