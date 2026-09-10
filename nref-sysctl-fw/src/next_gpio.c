/* GPIO extender on USB-C PD port board of MNT Reform Next */

#include "next_gpio.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "sysctl.h"

// PCA9536DP GPIO extender (on motherboard, i2c1), before rev R-1
#define PCA9536_ADDR 0x41

void pca9536_write_byte(uint8_t addr, uint8_t val) {
  sysctl_disable_irqs();
  uint8_t buf[2] = {addr, val};
  i2c_write_blocking(i2c1, PCA9536_ADDR, buf, 2, false);
  sysctl_enable_irqs();
}

uint8_t pca9536_read_byte(uint8_t addr) {
  sysctl_disable_irqs();
  uint8_t buf;
  i2c_write_blocking(i2c1, PCA9536_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c1, PCA9536_ADDR, &buf, 1, false);
  sysctl_enable_irqs();
  return buf;
}

// TCA6408APW GPIO extender (on motherboard, i2c1), since rev R-1
#define TCA6408_ADDR 0x20

void tca6408_write_byte(uint8_t addr, uint8_t val) {
  sysctl_disable_irqs();
  uint8_t buf[2] = {addr, val};
  i2c_write_blocking(i2c1, TCA6408_ADDR, buf, 2, false);
  sysctl_enable_irqs();
}

uint8_t tca6408_read_byte(uint8_t addr) {
  sysctl_disable_irqs();
  uint8_t buf;
  i2c_write_blocking(i2c1, TCA6408_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c1, TCA6408_ADDR, &buf, 1, false);
  sysctl_enable_irqs();
  return buf;
}

// PCA9557 GPIO extender (on usb-c pd board)
#define PCA9557_ADDR 0x19

static uint8_t gpio_mb_state;
static uint8_t gpio_ext_pd_state;

void pca9557_write_byte(uint8_t addr, uint8_t val) {
  sysctl_disable_irqs();
  uint8_t buf[2] = {addr, val};
  i2c_write_blocking(i2c0, PCA9557_ADDR, buf, 2, false);
  sysctl_disable_irqs();
}

uint8_t pca9557_read_byte(uint8_t addr) {
  sysctl_disable_irqs();
  uint8_t buf;
  i2c_write_blocking(i2c0, PCA9557_ADDR, &addr, 1, true);
  i2c_read_blocking(i2c0, PCA9557_ADDR, &buf, 1, false);
  sysctl_enable_irqs();
  return buf;
}

/* prerelease mainboards (pre R-1) */
void gpio_mb_dev_setup() {
  /*
    IO3: ~QON
    IO2: HDMI_DP_SWITCH
    IO1: 3V3_ENABLE
    IO0: 5V_ENABLE
  */

  gpio_mb_state = 0b0000;

  // config: all outputs
  pca9536_write_byte(3, 0b0000);
  // output port:
  pca9536_write_byte(1, gpio_mb_state);
}

void gpio_mb_dev_enable(uint8_t bit) {
  gpio_mb_state |= (1<<bit);
  pca9536_write_byte(1, gpio_mb_state);
}

void gpio_mb_dev_disable(uint8_t bit) {
  gpio_mb_state &= ~(1<<bit);
  pca9536_write_byte(1, gpio_mb_state);
}

/* release mainboards (R-1+) */
void gpio_mb_setup(int warm) {
  /*
    IO7: unused
    IO6: USWITCH_3
    IO5: USWITCH_2
    IO4: USWITCH_1
    IO3: ~QON
    IO2: HDMI_DP_SWITCH
    IO1: 3V3_ENABLE
    IO0: 5V_ENABLE
  */

  if (warm) {
    gpio_mb_state = 0b01000011;
  } else {
    gpio_mb_state = 0b01000000;
  }

  // output port:
  tca6408_write_byte(1, gpio_mb_state);
  // config: no inversion
  tca6408_write_byte(2, 0b00000000);
  // config: all outputs
  tca6408_write_byte(3, 0b00000000);
}

void gpio_mb_enable(uint8_t bit) {
  gpio_mb_state |= (1<<bit);
  tca6408_write_byte(1, gpio_mb_state);
}

void gpio_mb_disable(uint8_t bit) {
  gpio_mb_state &= ~(1<<bit);
  tca6408_write_byte(1, gpio_mb_state);
}

void gpio_ext_pd_setup() {
  /*
    IO7: 5V_AUX_EN
    IO6: ~LED_BLUE
    IO5: ~LED_RED
    IO4: ~USB_FAULT (input)
    IO3: ~USB_MUX_OE
    IO2: USB_MUX_DIR
    IO1: USB_SRC_EN
    IO0: USB_SINK_EN (open drain output!)
  */

  gpio_ext_pd_state = 0b00001101;

  // output port:
  pca9557_write_byte(1, gpio_ext_pd_state);
  // config: outputs(0)/inputs(1)
  pca9557_write_byte(3, 0b00010000);
}

#define BIT_5V_AUX_EN 7
#define BIT_NOT_LED_BLUE 6
#define BIT_NOT_LED_RED 5
#define BIT_NOT_USB_FAULT 4
#define BIT_NOT_USB_MUX_OE 3
#define BIT_USB_MUX_DIR 2
#define BIT_USB_SRC_EN 1
#define BIT_USB_SINK_EN 0

void gpio_ext_pd_enable(uint8_t bit) {
  gpio_ext_pd_state |= (1<<bit);
  pca9557_write_byte(1, gpio_ext_pd_state);
}

void gpio_ext_pd_disable(uint8_t bit) {
  gpio_ext_pd_state &= ~(1<<bit);
  pca9557_write_byte(1, gpio_ext_pd_state);
}

void gpio_ext_pd_poweroff_defaults() {
  gpio_ext_pd_disable(BIT_USB_SRC_EN);
  gpio_ext_pd_disable(BIT_5V_AUX_EN);
  gpio_ext_pd_enable(BIT_NOT_USB_MUX_OE);
  gpio_ext_pd_set_blue_led(0);
}

void gpio_ext_pd_poweron_defaults() {
  gpio_ext_pd_disable(BIT_USB_SRC_EN);
  gpio_ext_pd_disable(BIT_NOT_USB_MUX_OE);
  gpio_ext_pd_enable(BIT_5V_AUX_EN);
  gpio_ext_pd_set_blue_led(0);
  // FIXME: too bright. (pwm?)
  //gpio_ext_pd_set_blue_led(1);
}

void gpio_ext_pd_usb_5v_src_set(uint8_t enable) {
  if (enable) {
    //gpio_ext_pd_disable(BIT_USB_SINK_EN);
    gpio_ext_pd_enable(BIT_USB_SRC_EN);
  } else {
    gpio_ext_pd_disable(BIT_USB_SRC_EN);
    //gpio_ext_pd_enable(BIT_USB_SINK_EN);
  }
}

void gpio_ext_pd_set_red_led(uint8_t enable) {
  if (enable) {
    gpio_ext_pd_disable(BIT_NOT_LED_RED);
  } else {
    gpio_ext_pd_enable(BIT_NOT_LED_RED);
  }
}

void gpio_ext_pd_set_blue_led(uint8_t enable) {
  if (enable) {
    gpio_ext_pd_disable(BIT_NOT_LED_BLUE);
  } else {
    gpio_ext_pd_enable(BIT_NOT_LED_BLUE);
  }
}
