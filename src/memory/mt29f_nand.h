
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <sys/types.h>

typedef struct mt29f_cfg {
  uint8_t   num_dies;         // AKA "plane"
  uint16_t  blocks_per_die;
  uint16_t  pages_per_block;
  uint16_t  bytes_per_page;
  uint16_t  oob_bytes;
} mt29f_cfg_t;

/**
 * @brief This function initializes the flash device
 * @return 0 on success, -ENODEV if SPI is not ready or chip ID check fails
*/
int mt29f_init(void);


/**
 * @brief Get the flash device configuration
 * @return pointer to the configuration structure
*/
const mt29f_cfg_t* mt29f_get_config(void);


/**
 * @brief Read data from flash device
*/
int mt29f_read(const off_t offset, uint8_t *data, const size_t len);


/**
 * @brief Write data to flash device
*/
int mt29f_write(const off_t offset, const uint8_t *data, const size_t len);


/**
 * @brief Erases a single block
 * @param offset byte offset of any page within the target block
*/
void mt29f_block_erase(off_t offset);


/**
 * @brief Erases entire flash device
*/
void mt29f_chip_erase(void);


/**
 * @brief resets the flash device (unsure what it's actually doing)
 */
void mt29f_chip_reset(void);


/**
 * @brief Read the ONFI parameter page and return the manufacturer and model
 *        strings the die reports for itself.
 *
 * The READ ID device byte only identifies a part through a lookup table; this
 * reads the ASCII part number written at the factory, so it can confirm which
 * chip is physically fitted. Read-only with respect to the array.
 *
 * @param manufacturer  buffer, at least 13 bytes
 * @param man_len       size of @p manufacturer
 * @param model         buffer, at least 21 bytes
 * @param model_len     size of @p model
 * @return 0 on success, -ENOTSUP if the ONFI signature is absent, else -errno
 */
int mt29f_read_param_page(char *manufacturer, size_t man_len,
                          char *model, size_t model_len);
