
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <sys/types.h>

typedef struct mt29f_cfg {
  uint8_t   num_dies;         // AKA "plane"
  uint16_t  blocks_per_die;
  uint16_t  pages_per_block;
  uint16_t  bytes_per_page;   // address stride and SPI transfer size
  uint16_t  usable_bytes_per_page;  // how much of that a caller may fill
  uint16_t  oob_bytes;
} mt29f_cfg_t;

/*
 * bytes_per_page vs usable_bytes_per_page
 * --------------------------------------
 * A page is 2176 bytes on the wire and 2176 bytes of address space, but with
 * internal ECC enabled (SEC_STATUS_BIT_ECC_EN, set in mt29f_init) the die owns
 * the last 64 of them for parity: whatever a caller writes at 2112..2175 is
 * replaced by the ECC engine's own bytes. Measured on SN3 2026-08-26 — with
 * ECC on, bytes 0..2111 round-trip and 2112..2175 come back as parity; with
 * ECC off, all 2176 round-trip. See src/memory/nand_page_defects.md.
 *
 * So: address and transfer with bytes_per_page, but never put payload past
 * usable_bytes_per_page.
 */

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
 * @brief Read/write a feature register directly (REG_CONFIGURATION,
 *        REG_STATUS, ... from mt29f_defs.h).
 *
 * Exposed for the page-layout / ECC diagnostics in nvs_bringup.c — the driver
 * itself has no business toggling features after init.
 */
int mt29f_get_feature(uint8_t reg, uint8_t *val);
int mt29f_set_feature(uint8_t reg, uint8_t val);


/**
 * @brief STATUS register sampled immediately after the last page read.
 *
 * ECCS[2:0] live in bits 6:4 and say whether the on-die ECC engine corrected
 * the page, and how hard it had to work. Nothing checked this before
 * 2026-08-26, so an uncorrectable read was indistinguishable from a good one.
 * @return the raw status byte; use mt29f_ecc_status_of() to extract the code.
 */
uint8_t mt29f_last_read_status(void);

/** @brief Extract the 3-bit ECCS field from a STATUS byte. */
uint8_t mt29f_ecc_status_of(uint8_t status);

/**
 * @brief Number of page reads since boot whose ECCS field was not
 *        ECC_STATUS_OK, and the offset of the most recent one.
 */
uint32_t mt29f_ecc_event_count(void);
off_t mt29f_ecc_last_offset(void);


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
