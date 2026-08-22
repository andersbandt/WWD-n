//*****************************************************************************
//!
//! @file mt29f_nand.c
//! @author Anders Bandt
//! @brief MT29F driver
//! @version 0.9
//! @date January 2026
//!
//*****************************************************************************


/* Standard C99 stuff */
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

/* Zephyr files */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>


/* My header files  */
#include "mt29f_defs.h"
#include "mt29f_nand.h"



LOG_MODULE_REGISTER(mt29f_nand, CONFIG_LOG_DEFAULT_LEVEL);



#define DUMMY_BYTE      0x00
#define MAX_SUPPORTED_BUF  (4352)

#define DT_DRV_COMPAT nordic_nrf_spim

#define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(micron_mt29f)
#define SPI_OP SPI_OP_MODE_MASTER | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_WORD_SET(8) | SPI_LINES_SINGLE

static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);

/* Guards every public entry point below. The driver's internal state (spi_dev
 * transactions, die/plane select, wait-until-ready polling) assumes a single
 * caller mid-sequence; without this, the NVS pipeline thread (main.c heartbeat)
 * and any other caller on a separate thread (e.g. src/comm/protocol.c's flash
 * dump) can interleave transactions on the shared SPI1 bus and corrupt state
 * or wedge the driver. Recursive-safe (k_mutex tracks lock count per owning
 * thread), so nested calls from the same thread are fine. */
K_MUTEX_DEFINE(mt29f_bus_mutex);

typedef struct mt29f_row_addr {
  uint8_t   die_num;
  uint8_t   page_num;
  uint16_t  blk_num;
} mt29f_row_addr_t;

typedef uint16_t mt29f_col_addr_t;

// Flash hardware configuration
static const mt29f_cfg_t cfg = {
    .num_dies = 2,
    .blocks_per_die = 1024,
    .pages_per_block = 64,
    .bytes_per_page = 2176,
    .oob_bytes = 128
};

static mt29f_cfg_t inst = {0};


static int spi_nand_get_feature(const uint8_t addr, uint8_t *val)
{
  uint8_t tx_data[] = {COMMAND_GET_FEATURE, addr};

  struct spi_buf spi_buf[2] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    },
    {
      .buf = val,
      .len = 1
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 2,
	};

	const struct spi_buf_set rx_set = {
    .buffers = spi_buf,
    .count = 2,
	};

  return spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
}

static int spi_nand_set_feature(const uint8_t addr, const uint8_t val)
{
  uint8_t tx_data[] = {COMMAND_SET_FEATURE, addr, val};

  struct spi_buf spi_buf[] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 1,
	};

  return spi_write_dt(&spi_dev, &tx_set);
}

static int spi_nand_check_id(void)
{
  uint8_t const expected[2] = {MANUFACTURER_ID, DEVICE_ID};

  uint8_t tx_data[] = {COMMAND_READ_ID, DUMMY_BYTE};
  uint8_t read_id[2];

  struct spi_buf spi_buf[2] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    },
    {
      .buf = read_id,
      .len = ARRAY_SIZE(read_id),
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 2,
	};

	const struct spi_buf_set rx_set = {
    .buffers = spi_buf,
    .count = 2,
	};

  int ret = spi_transceive_dt(&spi_dev, &tx_set, &rx_set);

  if (memcmp(expected, read_id, ARRAY_SIZE(read_id)) != 0) {
    LOG_ERR("Wrong ID: %02X %02X , expected: %02X %02X",
        read_id[0], read_id[1], expected[0], expected[1]);

    /* A bare hex mismatch is not actionable during board bring-up — the same
     * message covers "no chip at all", "bus is broken" and "somebody fitted
     * the wrong part". Name the silicon instead. IDs per the Micron SPI NAND
     * table in the Linux MTD driver (drivers/mtd/nand/spi/micron.c); note the
     * low bit distinguishes supply voltage, so a 3.3 V part and its 1.8 V twin
     * are one bit apart and trivially confused on a BOM. */
    if (read_id[0] == MANUFACTURER_ID) {
      const char *part = NULL;
      bool low_voltage = false;

      switch (read_id[1]) {
      case 0x14: part = "MT29F1G01ABAFD 1Gb";                        break;
      case 0x15: part = "MT29F1G01ABAFD 1Gb"; low_voltage = true;    break;
      case 0x24: part = "MT29F2G01ABAGD 2Gb";                        break;
      case 0x25: part = "MT29F2G01ABBGD 2Gb"; low_voltage = true;    break;
      case 0x34: part = "MT29F4G01ABAFD 4Gb";                        break;
      case 0x35: part = "MT29F4G01ABBFD 4Gb"; low_voltage = true;    break;
      case 0x36: part = "MT29F4G01ADAGD 4Gb";                        break;
      case 0x46: part = "MT29F8G01ADAFD 8Gb";                        break;
      case 0x47: part = "MT29F8G01ADBFD 8Gb"; low_voltage = true;    break;
      default:   break;
      }

      if (part != NULL) {
        LOG_ERR("  chip is Micron %s (%s part)", part,
                low_voltage ? "1.8 V" : "3.3 V");
        if (low_voltage) {
          LOG_ERR("  *** WRONG PART: this board runs the NAND at 3.3 V. ***");
          LOG_ERR("  *** Fit MT29F2G01ABAGD (id 0x24). Do NOT widen this  ***");
          LOG_ERR("  *** check -- 3.3 V exceeds this part's max VCC.      ***");
        }
      } else {
        LOG_ERR("  unrecognised Micron device id -- check the part marking");
      }

      /* Settle it from the die rather than the table above. */
      {
        char man[16] = {0};
        char model[24] = {0};

        if (mt29f_read_param_page(man, sizeof(man), model, sizeof(model)) == 0) {
          LOG_ERR("  ONFI parameter page says: '%s' '%s'", man, model);
        } else {
          LOG_ERR("  ONFI parameter page unreadable -- id table above is all we have");
        }
      }
    } else {
      LOG_ERR("  manufacturer byte is not Micron (0x%02X) -- bus fault or no chip",
              read_id[0]);
    }
    return -ENODEV;
  }
  return ret;
}

static int spi_nand_die_select(uint8_t die_num)
{
  int rc = 0;
  const uint8_t target_die = (die_num == 0) ? DIE_0 : DIE_1;
  uint8_t die;
  spi_nand_get_feature(REG_DIE_SELECT, &die);
  if (die != target_die) {
    rc = spi_nand_set_feature(REG_DIE_SELECT, target_die);
    if (rc != 0) {
      LOG_ERR("Fail to select Die: %d", rc);
    }
  }

  LOG_DBG("Die select: %d", die_num);

  return rc;
}

static int spi_nand_reset(void) {
  uint8_t tx_data[] = {COMMAND_RESET};

  struct spi_buf spi_buf[] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 1,
	};

  return spi_write_dt(&spi_dev, &tx_set);
}

static int spi_nand_write_enable(void)
{
  uint8_t tx_data[] = {COMMAND_WRITE_ENABLE};

  struct spi_buf spi_buf[] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 1,
	};

  return spi_write_dt(&spi_dev, &tx_set);
}

static void spi_nand_unlock(uint8_t die_number)
{
  // Select Die
  {
    int rc = spi_nand_die_select(die_number);
    if (rc != 0) {
      LOG_ERR("Fail to select Die: %d", rc);
    }
    
  }

  spi_nand_write_enable();
  
  // Unlock the flash memory
  {
    int rc = spi_nand_set_feature(REG_BLOCK_LOCK, 0);

    if (rc != 0) {
      LOG_ERR("Fail to unlock NAND: %d", rc);
      return;
    }
  }
}

static int spi_nand_wait_until_ready(void)
{
  int ret;
  uint8_t reg = 0;

  do {
    ret = spi_nand_get_feature(REG_STATUS, &reg);
  } while (!ret && (reg & STATUS_BIT_OIP_MASK));

  return ret;
}

static mt29f_row_addr_t spi_nand_offset_to_row_addr(const off_t offset)
{
  mt29f_row_addr_t row_addr = {0};

  const uint64_t bytes_per_block = inst.bytes_per_page * inst.pages_per_block;
  const uint64_t bytes_per_die = bytes_per_block * inst.blocks_per_die;

  row_addr.die_num = offset / (bytes_per_die);
  row_addr.blk_num = (offset % bytes_per_die) / bytes_per_block;

  const off_t page_offset = (offset % bytes_per_die) % bytes_per_block;
  row_addr.page_num = (page_offset % bytes_per_block) / inst.bytes_per_page;

  return row_addr;
}

static void spi_nand_block_erase(const mt29f_row_addr_t addr)
{
  spi_nand_die_select(addr.die_num);

  spi_nand_write_enable();

  // Row address
  uint32_t address = (addr.blk_num & 0x07FF) << BLOCK_POS;
  address |= (addr.page_num & 0x003F) << PAGE_POS;

  uint8_t tx_data[] = {
    COMMAND_BLOCK_ERASE,
    (address >> 16) & 0xFF,
    (address >> 8) & 0xFF,
    address & 0xFF
  };

  struct spi_buf spi_buf[] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 1,
  };

  LOG_DBG("Block %d erase: %d", addr.blk_num, address);

  int ret = spi_write_dt(&spi_dev, &tx_set);
  if (ret != 0) {
    LOG_ERR("Block erase failed: %d", ret);
    return;
  }

  spi_nand_wait_until_ready();

  // E_FAIL is only valid after OIP clears; a silent erase failure would be
  // indistinguishable from a logic bug in the metadata scan.
  {
    uint8_t status = 0;
    if (spi_nand_get_feature(REG_STATUS, &status) == 0 &&
        (status & STATUS_BIT_ERASE_FAIL_MASK)) {
      LOG_ERR("Block %d erase FAILED (E_FAIL set, status=0x%02x)", addr.blk_num, status);
    }
  }
}

static int spi_nand_page_load(const uint32_t row_addr) {
    uint8_t tx_data[] = {
    COMMAND_PAGE_READ,
    (row_addr >> 16) & 0xFF,
    (row_addr >> 8) & 0xFF,
    row_addr & 0xFF
  };

  struct spi_buf spi_buf[] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 1,
  };

  LOG_DBG("Page load: %d", row_addr);

  return spi_write_dt(&spi_dev, &tx_set);
}

static int spi_nand_page_cache_read(const mt29f_col_addr_t col_addr, uint8_t *dest, const size_t len)
{
  uint8_t tx_data[] = {
    COMMAND_READ_FROM_CACHE_x1,
    (col_addr >> 8) & 0xFF,
    col_addr & 0xFF,
    DUMMY_BYTE
  };

  struct spi_buf spi_buf[2] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    },
    {
      .buf = dest,
      .len = len
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 2,
  };

  const struct spi_buf_set rx_set = {
    .buffers = spi_buf,
    .count = 2,
  };

  LOG_DBG("Cache read at %02X: %d bytes", col_addr, len);

  return spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
}

static int spi_nand_page_read(const off_t offset, uint8_t *dest, const size_t len)
{
  int rc = 0;

  mt29f_row_addr_t row_addr = spi_nand_offset_to_row_addr(offset);

  spi_nand_die_select(row_addr.die_num);

  LOG_DBG("Page read: %ld", offset);
  LOG_DBG("Die: %d; Blk: %d; Page: %d", row_addr.die_num, row_addr.blk_num, row_addr.page_num);

  // Row address select
  uint32_t address = (row_addr.blk_num & 0x07FF) << BLOCK_POS;
  address |= (row_addr.page_num & 0x003F) << PAGE_POS;

  rc = spi_nand_page_load(address);
  if (rc != 0) {
    LOG_ERR("Page Load Failed: %d", rc);
    return rc;
  }

  spi_nand_wait_until_ready();

  // Column address must carry the plane-select bit (CA12 = block LSB) or
  // this reads the wrong plane's cache register on odd blocks.
  const mt29f_col_addr_t col_addr = (row_addr.blk_num & 0x1) << COLUMN_PLANE_SELECT_POS;

  // This only reads 1 whole page at a time
  rc = spi_nand_page_cache_read(col_addr, dest, inst.bytes_per_page);
  if (rc != 0) {
    LOG_ERR("Page Cache Read Failed: %d", rc);
    return rc;
  }

  return rc;
}

static int spi_nand_program_load(const mt29f_col_addr_t col_addr, const uint8_t *data, const size_t len)
{
  uint8_t tx_data[] = {
    COMMAND_PROGRAM_LOAD_x1,
    (col_addr >> 8) & 0xFF,
    col_addr & 0xFF
  };

  struct spi_buf spi_buf[2] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    },
    {
      .buf = (void *)data,
      .len = len
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 2,
  };

  LOG_DBG("Program load at %02X: %d bytes", col_addr, len);

  return spi_write_dt(&spi_dev, &tx_set);
}

static int spi_nand_program_execute(const uint32_t mt29f_row_addr)
{
  uint8_t tx_data[] = {
    COMMAND_PROGRAM_EXECUTE,
    (mt29f_row_addr >> 16) & 0xFF,
    (mt29f_row_addr >> 8) & 0xFF,
    mt29f_row_addr & 0xFF
  };

  struct spi_buf spi_buf[] = {
    {
      .buf = tx_data,
      .len = ARRAY_SIZE(tx_data),
    }
  };

  const struct spi_buf_set tx_set = {
    .buffers = spi_buf,
    .count = 1,
  };

  LOG_DBG("Program execute page %d", mt29f_row_addr);

  return spi_write_dt(&spi_dev, &tx_set);
}

static int spi_nand_page_write(const off_t offset, const uint8_t *data, const size_t len)
{
  int rc = 0;

  mt29f_row_addr_t row_addr = spi_nand_offset_to_row_addr(offset);

  spi_nand_die_select(row_addr.die_num);
  spi_nand_write_enable();

  // Column address must carry the plane-select bit (CA12 = block LSB) or
  // this loads the wrong plane's cache register on odd blocks.
  const mt29f_col_addr_t col_addr = (row_addr.blk_num & 0x1) << COLUMN_PLANE_SELECT_POS;

  // This only writes 1 whole page at a time
  rc = spi_nand_program_load(col_addr, data, inst.bytes_per_page);
  if (rc != 0) {
    LOG_ERR("Page Program Load Failed: %d", rc);
    return rc;
  }

  spi_nand_write_enable();

  // Row address select
  uint32_t address = (row_addr.blk_num & 0x07FF) << BLOCK_POS;
  address |= (row_addr.page_num & 0x003F) << PAGE_POS;

  rc = spi_nand_program_execute(address);
  if (rc != 0) {
    LOG_ERR("Page Program Execute Failed: %d", rc);
    return rc;
  }

  spi_nand_wait_until_ready();

  // P_FAIL is only valid after OIP clears. Without this check a failed program
  // returns 0 and the corruption only surfaces at the next metadata scan.
  {
    uint8_t status = 0;

    rc = spi_nand_get_feature(REG_STATUS, &status);
    if (rc == 0 && (status & STATUS_BIT_PROGRAM_FAIL_MASK)) {
      LOG_ERR("Page program FAILED at offset %ld (P_FAIL set, status=0x%02x)",
              (long)offset, status);
      return -EIO;
    }
  }

  return rc;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
* @brief this function execute flash memory init on selected die.
*/
int mt29f_init(void)
{
  inst = cfg;

  if (!spi_is_ready_dt(&spi_dev)) {
    LOG_ERR("SPI device not initialized!");
    return -ENODEV;
  }

  // Reset the flash memory
  {
    int rc = spi_nand_reset();
    if (rc) {
      LOG_ERR("Fail to reset NAND! err: %d", rc);
    }
  }

  // Wait for power on reset (Datasheet value is equal to 1.25mSec)
  k_msleep(2);
  spi_nand_wait_until_ready();

  // Check ID response
  {
    int rc = spi_nand_check_id();
    if (rc) {
      LOG_ERR("Check ID Failed");
      return -ENODEV;
    }
  }

  spi_nand_write_enable();

  // Enable ECC
  {
    int rc = spi_nand_set_feature(REG_CONFIGURATION, SEC_STATUS_BIT_ECC_EN);
    if (rc != 0) {
      LOG_ERR("Set Feature Failed: %d", rc);
    }
  }

  spi_nand_unlock(DIE_1);
  spi_nand_unlock(DIE_0);
  spi_nand_wait_until_ready();

  LOG_INF("MT29F Init Complete");
  return 0;
}

const mt29f_cfg_t* mt29f_get_config(void)
{
  return &cfg;
}

int mt29f_read(const off_t offset, uint8_t *data, const size_t len)
{
  if (!data) {
    LOG_ERR("Invalid data buffer!");
    return -EINVAL;
  }

  if (len % inst.bytes_per_page != 0) {
    LOG_ERR("Read in blocks of page bytes!");
    return -EINVAL;
  }

  k_mutex_lock(&mt29f_bus_mutex, K_FOREVER);
  int rc = spi_nand_page_read(offset, data, len);
  k_mutex_unlock(&mt29f_bus_mutex);

  return rc;
}

int mt29f_write(const off_t offset, const uint8_t *data, const size_t len)
{
  if (!data) {
    LOG_ERR("Invalid data buffer!");
    return -EINVAL;
  }

  if (len % inst.bytes_per_page != 0) {
    LOG_ERR("Write in chunks of page bytes!");
    return -EINVAL;
  }

  k_mutex_lock(&mt29f_bus_mutex, K_FOREVER);
  int rc = spi_nand_page_write(offset, data, len);
  k_mutex_unlock(&mt29f_bus_mutex);

  return rc;
}

void mt29f_block_erase(off_t offset)
{
  mt29f_row_addr_t addr = spi_nand_offset_to_row_addr(offset);

  k_mutex_lock(&mt29f_bus_mutex, K_FOREVER);
  spi_nand_block_erase(addr);
  k_mutex_unlock(&mt29f_bus_mutex);
}

void mt29f_chip_erase(void)
{
  LOG_INF("Erasing NAND chip...");

  int total_blocks = inst.num_dies * inst.blocks_per_die;

  k_mutex_lock(&mt29f_bus_mutex, K_FOREVER);
  for (int i = 0; i < inst.num_dies; i++) {
    for (int j = 0; j < inst.blocks_per_die; j++) {
      int block_num = i * inst.blocks_per_die + j;
      if (block_num % 512 == 0) {
        LOG_INF("Erasing block %d/%d", block_num, total_blocks);
      }
      mt29f_row_addr_t addr = {.die_num = i, .blk_num = j, .page_num = 0};
      spi_nand_block_erase(addr);
    }
  }
  k_mutex_unlock(&mt29f_bus_mutex);

  LOG_INF("Erase complete: %d blocks erased", total_blocks);
}


void mt29f_chip_reset(void) {
  k_mutex_lock(&mt29f_bus_mutex, K_FOREVER);
  spi_nand_reset();
  k_mutex_unlock(&mt29f_bus_mutex);
}

#define MT29F_CFG_OTP_EN        0x40
#define MT29F_PARAM_PAGE_ROW    0x01

/*
 * Read the ONFI parameter page and print the part number the die itself
 * reports. The READ ID device byte only identifies the part via a lookup
 * table; the parameter page carries the manufacturer and model as ASCII
 * strings written at the factory, so it settles "which chip is actually
 * fitted" without trusting any table.
 *
 * Sequence per the Micron SPI NAND datasheet: set OTP_EN in the configuration
 * register, PAGE READ from row 0x01, read the cache out, then restore the
 * configuration register. Read-only with respect to the array.
 *
 * ONFI parameter page layout: bytes 0-3 = "ONFI" signature, 32-43 =
 * manufacturer, 44-63 = device model.
 */

int mt29f_read_param_page(char *manufacturer, size_t man_len,
                          char *model, size_t model_len)
{
  uint8_t cfg = 0, cfg_saved = 0;
  uint8_t page[64] = {0};
  int rc;

  if (manufacturer == NULL || model == NULL ||
      man_len < 13 || model_len < 21) {
    return -EINVAL;
  }

  rc = spi_nand_get_feature(REG_CONFIGURATION, &cfg);
  if (rc != 0) {
    return rc;
  }
  cfg_saved = cfg;

  rc = spi_nand_set_feature(REG_CONFIGURATION, cfg | MT29F_CFG_OTP_EN);
  if (rc != 0) {
    return rc;
  }

  rc = spi_nand_page_load(MT29F_PARAM_PAGE_ROW);
  if (rc == 0) {
    spi_nand_wait_until_ready();
    rc = spi_nand_page_cache_read(0, page, sizeof(page));
  }

  /* Always restore the configuration register, even on a failed read. */
  spi_nand_set_feature(REG_CONFIGURATION, cfg_saved);

  if (rc != 0) {
    return rc;
  }

  if (page[0] != 'O' || page[1] != 'N' || page[2] != 'F' || page[3] != 'I') {
    return -ENOTSUP;
  }

  memcpy(manufacturer, &page[32], 12);
  manufacturer[12] = '\0';
  memcpy(model, &page[44], 20);
  model[20] = '\0';

  /* The fields are space-padded; trim so the log line is readable. */
  for (int i = 11; i >= 0 && (manufacturer[i] == ' ' || manufacturer[i] == '\0'); i--) {
    manufacturer[i] = '\0';
  }
  for (int i = 19; i >= 0 && (model[i] == ' ' || model[i] == '\0'); i--) {
    model[i] = '\0';
  }

  return 0;
}

