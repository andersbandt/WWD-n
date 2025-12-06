
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(mt29f_nand, LOG_LEVEL_INF);

#include "mt29f_defs.h"
#include "mt29f_nand.h"

#define DUMMY_BYTE      0x00
#define MAX_SUPPORTED_BUF  (4352)

#define DT_DRV_COMPAT nordic_nrf_spim

#define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(micron_mt29f)
// #define SPI_DEV DT_NODELABEL(mt29f)
#define SPI_OP SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8) | SPI_LINES_SINGLE

static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);

// typedef struct mt29f_row_addr {
//   uint8_t   die_num;
//   uint8_t   page_num;
//   uint16_t  blk_num;
// } mt29f_row_addr_t;

// typedef uint16_t mt29f_col_addr_t;

// static mt29f_cfg_t inst = {0};



