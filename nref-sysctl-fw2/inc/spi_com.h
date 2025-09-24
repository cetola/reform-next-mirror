#ifndef _POCKET_SPICOM_H
#define _POCKET_SPICOM_H

#include <stdint.h>
#include "sysctl.h"

#define SPI_BUF_LEN 0x8
#define ST_EXPECT_DIGIT_0 0
#define ST_EXPECT_DIGIT_1 1
#define ST_EXPECT_DIGIT_2 2
#define ST_EXPECT_DIGIT_3 3
#define ST_EXPECT_CMD 4
#define ST_SYNTAX_ERROR 5
#define ST_EXPECT_RETURN 6
#define ST_EXPECT_MAGIC 7

void init_spi_client();

void handle_spi_commands(battery_info_s *battery_info);

#endif