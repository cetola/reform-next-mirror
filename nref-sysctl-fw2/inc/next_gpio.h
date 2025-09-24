/* GPIO extender on USB-C PD port board of MNT Reform Next */

#include <stdint.h>

#define GPIO_EXT_5V_EN 0
#define GPIO_EXT_3V3_EN 1
#define GPIO_EXT_HDMI_DP 2
#define GPIO_EXT_NQON 3

void gpio_ext_setup();
void gpio_ext_enable(uint8_t bit);
void gpio_ext_disable(uint8_t bit);
