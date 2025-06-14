/* GPIO extender on USB-C PD port board of MNT Reform Next */

#include "next_gpio.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// PCA9536DP GPIO extender (on motherboard, i2c1)
#define PCA9536_ADDR 0x41

// PCAL6416AHF GPIO extender (on usb-c pd board)
#define PCAL_ADDR 0x20

static uint8_t gpio_ext_state;

void pca9536_write_byte(uint8_t addr, uint8_t val) {
  uint8_t buf[2] = {addr, val};
  i2c_write_blocking(i2c1, PCA9536_ADDR, buf, 2, false);
}

uint8_t pca9536_read_byte(uint8_t addr) {
  uint8_t buf;
  i2c_write_blocking(i2c1, PCA9536_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c1, PCA9536_ADDR, &buf, 1, false);
  return buf;
}

void gpio_ext_setup() {
  /*
    IO0: 5V_ENABLE
    IO1: 3V3_ENABLE
    IO2: HDMI_DP_SWITCH
    IO3: ~QON
  */

  gpio_ext_state = 0b0000;

  // config: all outputs
  pca9536_write_byte(3, 0b0000);
  // output port:
  pca9536_write_byte(1, gpio_ext_state);
}

void gpio_ext_enable(uint8_t bit) {
  gpio_ext_state |= (1<<bit);
  pca9536_write_byte(1, gpio_ext_state);
}

void gpio_ext_disable(uint8_t bit) {
  gpio_ext_state &= ~(1<<bit);
  pca9536_write_byte(1, gpio_ext_state);
}
