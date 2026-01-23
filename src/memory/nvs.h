//*****************************************************************************
//!
//! @file nvs.h
//! @author Anders Bandt
//! @brief Anders' function for interfacing with the flash memory through NVS TI driver
//! @version 0.9
//! @date March 2024
//!
//*****************************************************************************

#ifndef SRC_DATA_NVS_H_
#define SRC_DATA_NVS_H_


// header files
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <mt29f_nand.h>


// define flash parameters (mainly size stuff)
static const mt29f_cfg_t cfg = {
    .num_dies = 2,
    .blocks_per_die = 1024,
    .pages_per_block = 64,
    .bytes_per_page = 2176,
    .oob_bytes = 128
};

static uint64_t flash_size = (uint64_t)cfg.num_dies * cfg.blocks_per_die * cfg.pages_per_block * cfg.bytes_per_page;


#define TOTAL_PAGES         cfg.num_dies * cfg.blocks_per_die * cfg.pages_per_block


// define NVS META info
#define META_BLOCK_COUNT    8
#define TOTAL_BLOCKS        (cfg.blocks_per_die * cfg.num_dies)
#define META_BLOCK_START    (TOTAL_BLOCKS - META_BLOCK_COUNT)



struct log_state {
    uint32_t magic;
    uint32_t seq;
    uint64_t nand_offset;
    uint32_t crc;
};


enum record_type {
    SAMPLE,
    TIME_ANCHOR,
    RESET_MARKER,
};

#define MAGIC_MARKER 0xACACAC


struct log_entry_hdr {
    uint16_t record_type;  // type of record
    uint16_t length;       // length of data
    uint16_t dt_ticks;    // timestamp of record
} __packed;



/**
 * @brief initializes the NVS handle
 */
void nvs_init();


/*
 * @brief close the NVS handle
 */
void nvs_close();


/**
 * @brief erases the whole NVS region
 */
void nvs_erase_region();


/**
 * @brief calculates address offset
 *
 * @desc reads through flash memory until sequence of 0xFF is found
 */
bool nvs_calc_offset();


/**
 * @brief getter function for address offset
 */
int nvs_get_addr_offset();


/**
 * @brief performs an NVS write
 */
int nvs_write(off_t addr, void * data, size_t len);


/**
 * @brief performs an NVS write at calculated "fresh address offset"
 */
int nvs_write_auto_offset(void * data, size_t num_bytes);


/**
 * @brief reads the NVS
 */
int nvs_read(off_t addr, void * buffer, size_t len);


/**
 * @brief logs a record to NVS
 * @param[in]   entry   pointer to log entry structure
 * @param[in]   type    type of record being logged
 */
int nvs_log_record(enum record_type type, const void *payload, uint16_t length, uint16_t dt_ticks);




#endif /* SRC_DATA_NVS_H_ */
