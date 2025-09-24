/**
 * SPI commands from the SOM
 *
 * Ported from MNT Reform reform2-lpc-fw.
 */

#include <stdio.h>
#include <string.h>
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "spi_com.h"

void init_spi_client()
{
  gpio_set_function(PIN_SOM_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_SS0, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SOM_SCK, GPIO_FUNC_SPI);

  // 4 MHz
  spi_init(spi1, 4000 * 1000);
  // we don't appreciate the wording, but it's the API we are given
  spi_set_slave(spi1, true);
  spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);

  printf("# [spi] init_spi_client done\n");
}

static int spi_debug_enabled = 0;

void handle_spi_commands(battery_info_s *battery_info)
{
  uint8_t spi_command = 0;
  uint8_t spi_arg1 = 0;
  uint8_t spi_buf[SPI_BUF_LEN]; // normally 8 bytes
  int spi_rxlen = 0;
  struct BatteryPack* packs = battery_info->packs;

  if (!battery_info->som_is_powered) return;
  if (!spi_is_readable(spi1)) return;

  // non blocking read
  for (uint8_t i = 0; i < 4; i++) {
    // read a byte (but don't write a byte)
    uint8_t rx = (uint8_t)spi_get_hw(spi1)->dr;
    spi_buf[i] = rx;
    spi_rxlen++;
  }

  // commands are always 4 bytes, starting with 0xb5
  // dump the buffer to serial
  if (spi_debug_enabled || spi_buf[0] != 0xb5 || spi_rxlen != 4) {
    printf("# [spi rx %d] ", spi_rxlen);
    for (int i = 0; i < spi_rxlen; i++) {
      printf("%2x ", spi_buf[i]);
    }
    printf("\t");
    for (int i = 0; i < spi_rxlen; i++) {
      if (spi_buf[i] >= 32) {
        printf("%c", spi_buf[i]);
      } else {
        printf(".");
      }
    }
    printf("\n");
    // reset SPI0 block
    // this is a workaround for confusion with
    // software spi from BPI-CM4 where we get
    // bit-shifted bytes
    printf("# [spi resync]\n");
    init_spi_client();
    return;
  }

  spi_command = spi_buf[1];
  spi_arg1 = spi_buf[2];

  // clear receive buffer, reuse as send buffer
  memset(spi_buf, 0, SPI_BUF_LEN);

  if (spi_command == 'f') {
    // return firmware version and api info
    if (spi_arg1 == 0) memcpy(spi_buf, FW_STRING1, MIN(SPI_BUF_LEN, sizeof(FW_STRING1)));
    else if (spi_arg1 == 1) memcpy(spi_buf, FW_STRING2, MIN(SPI_BUF_LEN, sizeof(FW_STRING2)));
    else memcpy(spi_buf, MNTRE_FIRMWARE_VERSION, MIN(SPI_BUF_LEN, sizeof(MNTRE_FIRMWARE_VERSION)));
  }
  else if (spi_command == 'q') {
    // execute status query command
    float gauge_percent = 0.0;
    float mV = 0.0;
    int mA = (int)(packs[0].ampere*1000.0 + packs[1].ampere*1000.0);
    int num_packs = 0;
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

    uint8_t percentage = (uint8_t)gauge_percent;
    int16_t voltsInt = (int16_t)(mV/1000.0);
    int16_t currentInt = (int16_t)(mA/1000.0);

    spi_buf[0] = (uint8_t)voltsInt;
    spi_buf[1] = (uint8_t)(voltsInt >> 8);
    spi_buf[2] = (uint8_t)currentInt;
    spi_buf[3] = (uint8_t)(currentInt >> 8);
    spi_buf[4] = (uint8_t)percentage;
    // TODO "state" not implemented
    spi_buf[5] = (uint8_t)0;
  }
  else if (spi_command == 'v') {
    // get cell voltage
    int volts = 0;
    uint8_t id = 0;

    if (spi_arg1 == 1) {
      id = 1;
    }

    for (uint8_t c = 0; c < 4; c++) {
      volts = packs[id].cells_v[c];
      spi_buf[c*2] = (uint8_t)volts;
      spi_buf[(c*2)+1] = (uint8_t)(volts >> 8);
    }
  }
  else if (spi_command == 'c') {
    // get calculated capacity (emulated)
    uint16_t cap_cur = (uint16_t)((packs[0].coulomb_cur + packs[1].coulomb_cur) / 3.6);
    uint16_t cap_min = (uint16_t)0; // deprecated
    uint16_t cap_max = (uint16_t)((packs[0].coulomb_max + packs[1].coulomb_max) / 3.6);

    spi_buf[0] = (uint8_t)cap_cur;
    spi_buf[1] = (uint8_t)(cap_cur >> 8);
    spi_buf[2] = (uint8_t)cap_min;
    spi_buf[3] = (uint8_t)(cap_min >> 8);
    spi_buf[4] = (uint8_t)cap_max;
    spi_buf[5] = (uint8_t)(cap_max >> 8);
  }
  else if (spi_command == 'p') {
    // toggle system power off
    if (spi_arg1 == 1) {
      turn_som_power_off();
      // don't try to send a response to turned-off SOM,
      // because SPI will hang otherwise
      return;
    }
  }
  else if (spi_command == 'z') {
    // pass message byte (spi_arg1) directly to uart (implemented for Desktop Reform control panel)
    /* TODO: not yet implemented */
  }
  else if (spi_command == 'b') {
    // TODO: display brightness
  }

  /* send response to host (8 bytes) and discard response */
  if (battery_info->som_is_powered) {
    spi_write_blocking(spi1, (const uint8_t*)spi_buf, 8);
  }
}
