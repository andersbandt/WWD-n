//*****************************************************************************
//!
//! @file nvs.c
//! @author Anders Bandt
//! @brief Controls the non-volatile memory (NVS) of the system
//! @version 0.9
//! @date March 2024
//!
//*****************************************************************************

/* Standard C99 stuff */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* My header files  */
#include <nvs.h>
#include <mt29f_nand.h>


LOG_MODULE_REGISTER(nvs, LOG_LEVEL_INF);


bool addr_status = 0;
static off_t write_addr = 0;
static off_t read_addr = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void nvs_init(void)
{
    mt29f_init(&cfg);

    // write a reset marker at the start of NVS
    offset_status = nvs_calc_offset();
    if (offset_status) {
        LOG_INF("NVS offset calculated at: [%d]", addr_offset);
        nvs_log_record(NULL, RESET_MARKER);

    } else {
        LOG_INF("NVS offset calculation failed");
    }
}


/*
 * nvs_close: closes an NVS instance
 */
void nvs_close() {
    spi_nand_reset();
}


/*
 * nvs_erase_region: erases the whole NVS region
 */
void nvs_erase_region() {
    LOG_INFO("Erasing flash REGION... like the whole thing...\n");

    mt29f_chip_erase();

    // regenerate the address offset
    nvs_calc_offset();
    LOG_INFO("\tnew write offset set at: [%d]", addr_offset);
}


/*
 * nvs_write: performs a write on an NVS memory instance
 */
int nvs_write(off_t addr, void * data, size_t len) {
    // perform some gross-check on address range
        if (addr < 0 || (uint64_t)addr >= flash_size) {
        return MT29F_ROW_ADDR_INVALID;
    }

    int status = mt29f_write(addr, data, len);
    return status;
}


/*
 * nvs_write_auto_offsets: performs a write operation with auto-increment of the "fresh space" offset
 */
int nvs_write_auto_offset(void * data, size_t len) {
    int status = nvs_write(addr_offset, data, len);

    addr_offset += len; // increment the offset up by `len` bytes
    return status;
}


// TODO: complete this function where it writes metadata information
int nvs_write_auto_offset_new(void * data, size_t len) {
    int status = nvs_write(addr_offset, data, len);

    addr_offset += len; // increment the offset up by `len` bytes
    return status;
}


/*
 * nvs_read: performs a read on an NVS memory instance
 */
int nvs_read(off_t addr, void * buffer, size_t len) {
    mt29f_read(addr, buffer, len);
    return len;
}


/*
 * nvs_get_offset: calculates the NVS address offset
 */
bool nvs_calc_offset() {
    // METHOD 1: searching for 0xFF (BAD)
    // look for unsigned int (0xFF / 255)
    uint8_t read_data[cfg.bytes_per_page];


    // sweep whole region size
    for (int page = 0; page < TOTAL_PAGES; page++) {
        nvs_read(page, read_data, cfg.bytes_per_page);

        // walk records inside page
                /* Walk records inside page */
        for (uint32_t r = 0; r < cfg.bytes_per_page; r++) {
            uint8_t *rec = &read_data[r];

            if (rec[0] == 0xFF &&
                rec[1] == 0xFF &&
                rec[2] == 0xFF) {

                addr_offset = (page * cfg.bytes_per_page) + r;
                return true;
            }
        }
    }
    return false;

    // METHOD 2: retrieving my stored pointers
    // static uint8_t data[2176];
    // int ret;
    // for (int block = META_BLOCK_START; block < TOTAL_BLOCKS; block++) {
    //     for (int page = 0; page < cfg.pages_per_block; page++) {
    //         ret = nvs_read(data, 2176, 0);
    //         if (ret != 0) {
    //             return false;
    //         }
    //         else {
    //             // TODO: have method for extracting meta information from raw page read
    //         }
    //     }
    // }
    // return true;
}


/*
 * nvs_get_addr_offset: "Getter" function for the address offset
 */
int nvs_get_addr_offset() {
    return addr_offset;
}


/*
 * nvs_log_record: logs a record to NVS
 */
// TODO: this needs to check if the record is the size of one page?
int nvs_log_record(enum record_type type, const void *payload, uint16_t length, uint16_t dt_ticks)
{
    struct log_record_hdr hdr = {
        .type = type,
        .length = length,
        .dt_ticks = dt_ticks,
    };

    nvs_write_auto_offset(&hdr, sizeof(hdr));
    nvs_write_auto_offset(payload, length);

    return 0;
}







