#ifndef _NEXT_GPIO_H
#define _NEXT_GPIO_H

/* GPIO extender on USB-C PD port board of MNT Reform Next */

#include <stdint.h>

#define GPIO_EXT_5V_EN 0
#define GPIO_EXT_3V3_EN 1
#define GPIO_EXT_HDMI_DP 2
#define GPIO_EXT_NQON 3
#define GPIO_EXT_USWITCH_1 4
#define GPIO_EXT_USWITCH_2 5
#define GPIO_EXT_USWITCH_3 6

void gpio_mb_setup(int warm);
void gpio_mb_enable(uint8_t bit);
void gpio_mb_disable(uint8_t bit);

void gpio_ext_pd_setup();
void gpio_ext_pd_enable(uint8_t bit);
void gpio_ext_pd_disable(uint8_t bit);
void gpio_ext_pd_poweroff_defaults();
void gpio_ext_pd_poweron_defaults();
void gpio_ext_pd_usb_5v_src_set(uint8_t enable);
void gpio_ext_pd_set_red_led(uint8_t enable);
void gpio_ext_pd_set_blue_led(uint8_t enable);

#endif
