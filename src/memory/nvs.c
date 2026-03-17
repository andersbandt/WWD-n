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
#include <stddef.h>
#include <unistd.h>
#include <errno.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

/* My header files  */
#include <nvs.h>
#include <mt29f_nand.h>
#include "../peripheral/clock.h"


LOG_MODULE_REGISTER(nvs, LOG_LEVEL_INF);


bool addr_status = false;
static off_t write_addr = 0;
// static off_t read_addr = 0;
static uint32_t metadata_seq = 0;  // Current metadata sequence number

// Flash configuration pointer (obtained from mt29f driver)
static const mt29f_cfg_t *cfg = NULL;

// Calculated flash parameters (initialized in nvs_init)
static uint32_t META_BLOCK_START = 0;
static uint64_t flash_size = 0;

// Page buffer for accumulating log records before writing to flash
static uint8_t page_buffer[2176];  // Match cfg->bytes_per_page
static uint16_t page_buffer_offset = 0;  // Current position in page buffer


static int nvs_flush_page_buffer(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void nvs_init()
{
    if (mt29f_init() != 0) {
        LOG_ERR("MT29F init failed, NVS unavailable");
        addr_status = false;
        return;
    }

    // Get flash configuration from driver
    cfg = mt29f_get_config();

    // Calculate flash parameters
    uint32_t total_blocks = cfg->blocks_per_die * cfg->num_dies;
    META_BLOCK_START = total_blocks - META_BLOCK_COUNT;
    flash_size = (uint64_t)cfg->num_dies * cfg->blocks_per_die * cfg->pages_per_block * cfg->bytes_per_page;

    // Initialize page buffer
    page_buffer_offset = 0;
    memset(page_buffer, 0xFF, sizeof(page_buffer));

    // erase chip
    //nvs_erase_chip();

    // Calculate write offset from metadata
    addr_status = nvs_calc_offset();
    if (addr_status) {
        LOG_INF("NVS initialized: offset=%ld, seq=%u", write_addr, metadata_seq);

        // Log a reset marker to indicate system startup
        // uint32_t marker = MAGIC_MARKER;
        // int ret = nvs_log_record(RESET_MARKER, &marker, sizeof(marker), get_dt_ticks());
        // if (ret != 0) {
        //     LOG_ERR("Failed to write reset marker: %d", ret);
        // }


    } else {
        LOG_ERR("NVS offset calculation failed");
    }
}


/*
 * nvs_close: closes an NVS instance
 */
void nvs_close() {
    LOG_INF(">>> nvs_close() CALLED");
    nvs_flush_buffer();  // Flush any pending data
    mt29f_chip_reset();
}


/*
 * nvs_flush_buffer: public wrapper to flush page buffer
 */
int nvs_flush_buffer(void) {
    return nvs_flush_page_buffer();
}


/*
 * nvs_erase_region: erases the whole NVS region
 */
void nvs_erase_chip() {
    LOG_INF("Erasing flash REGION... like the whole thing...\n");

    mt29f_chip_erase();

    // Wait for erase to fully complete
    k_msleep(100);

    // Reset page buffer and write address
    page_buffer_offset = 0;
    write_addr = 0;
    metadata_seq = 0;

    LOG_INF("Flash erased, offset reset to 0");
}


/*
 * nvs_write: performs a write on an NVS memory instance
 */
int nvs_write(off_t addr, void * data, size_t len) {
    // perform some gross-check on address range
        if (addr < 0 || (uint64_t)addr >= flash_size) {
            return -1;
        // return MT29F_ROW_ADDR_INVALID;
    }

    int status = mt29f_write(addr, data, len);
    return status;
}


/*
 * nvs_write_auto_offsets: performs a write operation with auto-increment of the "fresh space" offset
 */
int nvs_write_auto_offset(void * data, size_t len) {
    int status = nvs_write(write_addr, data, len);

    write_addr += len; // increment the offset up by `len` bytes
    return status;
}


// TODO: complete this function where it writes metadata information
int nvs_write_auto_offset_new(void * data, size_t len) {
    int status = nvs_write(write_addr, data, len);

    write_addr += len; // increment the offset up by `len` bytes
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
 * nvs_write_metadata: writes metadata to META blocks with wear leveling
 */
int nvs_write_metadata(uint64_t offset)
{
    struct log_state state;

    LOG_INF(">>> METADATA WRITE START: current_seq=%u, offset=%llu", metadata_seq, offset);

    // Populate metadata structure
    state.magic = MAGIC_MARKER;
    state.seq = metadata_seq++;
    state.nand_offset = offset;

    // Calculate CRC32 over magic, seq, and offset (exclude crc field itself)
    state.crc = crc32_ieee((uint8_t*)&state, offsetof(struct log_state, crc));

    LOG_INF("CRC input bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            ((uint8_t*)&state)[0], ((uint8_t*)&state)[1], ((uint8_t*)&state)[2], ((uint8_t*)&state)[3],
            ((uint8_t*)&state)[4], ((uint8_t*)&state)[5], ((uint8_t*)&state)[6], ((uint8_t*)&state)[7],
            ((uint8_t*)&state)[8], ((uint8_t*)&state)[9], ((uint8_t*)&state)[10], ((uint8_t*)&state)[11],
            ((uint8_t*)&state)[12], ((uint8_t*)&state)[13], ((uint8_t*)&state)[14], ((uint8_t*)&state)[15]);

    // Determine which block and page within that block to write to
    uint32_t total_meta_pages = (uint32_t)META_BLOCK_COUNT * cfg->pages_per_block;
    uint32_t meta_page_index  = state.seq % total_meta_pages;
    uint32_t block_offset     = meta_page_index / cfg->pages_per_block;
    uint32_t page_within_block = meta_page_index % cfg->pages_per_block;
    uint32_t meta_block       = META_BLOCK_START + block_offset;
    off_t meta_addr = (off_t)(meta_block * cfg->pages_per_block + page_within_block)
                      * cfg->bytes_per_page;

    // Erase block when starting its first page
    if (page_within_block == 0) {
        off_t block_addr = (off_t)meta_block * cfg->pages_per_block * cfg->bytes_per_page;
        LOG_INF("Erasing meta block %u before first write", meta_block);
        mt29f_block_erase(block_addr);
    }

    LOG_INF("Writing metadata: magic=0x%08X, seq=%u, offset=%llu, crc=0x%08X, block=%u, page=%u, addr=%ld",
            state.magic, state.seq, state.nand_offset, state.crc, meta_block, page_within_block, meta_addr);

    // Allocate page-sized buffer and pad with 0xFF (erased NAND state)
    uint8_t *page_buf = k_malloc(cfg->bytes_per_page);
    if (!page_buf) {
        LOG_ERR("Failed to allocate page buffer");
        return -ENOMEM;
    }

    memset(page_buf, 0xFF, cfg->bytes_per_page);
    memcpy(page_buf, &state, sizeof(state));

    // Write full page to META block
    int ret = mt29f_write(meta_addr, page_buf, cfg->bytes_per_page);
    k_free(page_buf);

    if (ret != 0) {
        LOG_ERR("Failed to write metadata: %d", ret);
        return ret;
    }
    else {
        LOG_INF("Wrote metadata!");
    }

    return 0;
}


/*
 * nvs_read_metadata: reads metadata from META blocks and finds the latest valid one
 */
int nvs_read_metadata(uint64_t *offset)
{
    struct log_state state;
    struct log_state best_state = {0};
    bool found_valid = false;

    // Allocate page-sized buffer for reading
    uint8_t *page_buf = k_malloc(cfg->bytes_per_page);
    if (!page_buf) {
        LOG_ERR("Failed to allocate page buffer for metadata read");
        return -ENOMEM;
    }

    // Phase 1: scan page 0 of each META block to find the active block
    int best_block_index = -1;
    for (int i = 0; i < META_BLOCK_COUNT; i++) {
        uint32_t meta_block = META_BLOCK_START + i;
        off_t meta_addr = (off_t)meta_block * cfg->pages_per_block * cfg->bytes_per_page;

        LOG_INF("Reading from meta block %u (addr=%ld)", meta_block, meta_addr);
        if (mt29f_read(meta_addr, page_buf, cfg->bytes_per_page) != 0) {
            LOG_WRN("Failed to read metadata from block %u", meta_block);
            continue;
        }

        memcpy(&state, page_buf, sizeof(state));

        if (state.magic != MAGIC_MARKER) {
            LOG_INF("Block %u: invalid magic (0x%08X)", meta_block, state.magic);
            continue;
        }

        uint32_t calculated_crc = crc32_ieee((uint8_t*)&state, offsetof(struct log_state, crc));
        if (calculated_crc != state.crc) {
            LOG_WRN("Block %u: CRC mismatch (calc=0x%08X, stored=0x%08X)",
                    meta_block, calculated_crc, state.crc);
            continue;
        }

        LOG_INF("Block %u: valid page 0, seq=%u", meta_block, state.seq);

        if (!found_valid || state.seq > best_state.seq) {
            best_state = state;
            found_valid = true;
            best_block_index = i;
        }
    }

    // Phase 2: scan remaining pages within the active block to find the last valid entry
    if (found_valid) {
        uint32_t meta_block = META_BLOCK_START + best_block_index;
        for (int p = 1; p < cfg->pages_per_block; p++) {
            off_t meta_addr = (off_t)(meta_block * cfg->pages_per_block + p)
                              * cfg->bytes_per_page;

            if (mt29f_read(meta_addr, page_buf, cfg->bytes_per_page) != 0) {
                break;
            }

            memcpy(&state, page_buf, sizeof(state));

            if (state.magic != MAGIC_MARKER) {
                break;  // Unwritten page, stop
            }

            uint32_t calculated_crc = crc32_ieee((uint8_t*)&state, offsetof(struct log_state, crc));
            if (calculated_crc != state.crc) {
                break;
            }

            best_state = state;
            LOG_INF("Block %u page %d: valid seq=%u", meta_block, p, state.seq);
        }
    }

    k_free(page_buf);

    if (!found_valid) {
        LOG_INF("No valid metadata found, starting fresh");
        *offset = 0;
        metadata_seq = 0;
        return -ENOENT;
    }

    // Found valid metadata
    *offset = best_state.nand_offset;
    metadata_seq = best_state.seq + 1;  // Next write will increment from this

    LOG_INF("Recovered metadata: seq=%u, offset=%llu", best_state.seq, *offset);
    return 0;
}


/*
 * nvs_calc_offset: calculates the NVS address offset using metadata
 */
bool nvs_calc_offset() {
    uint64_t offset;

    // Try to read metadata from META blocks
    int ret = nvs_read_metadata(&offset);
    if (ret == 0) {
        // Successfully recovered offset from metadata
        write_addr = (off_t)offset;
        LOG_INF("Offset recovered from metadata: %ld", write_addr);
        return true;
    }

    // No valid metadata found - this is a fresh/erased flash
    // Start writing from beginning of data region (block 0)
    write_addr = 0;
    LOG_INF("No metadata found, starting at offset 0");

    // Write initial metadata
    ret = nvs_write_metadata(write_addr);
    if (ret != 0) {
        LOG_ERR("Failed to write initial metadata");
        return false;
    }

    return true;
}


/*
 * nvs_get_write_addr: "Getter" function for the address offset
 */
int nvs_get_addr_offset() {
    return write_addr;
}


/*
 * nvs_flush_page_buffer: flushes accumulated records to flash
 */
int nvs_flush_page_buffer(void)
{
    if (page_buffer_offset == 0) {
        return 0;  // Nothing to flush
    }

    // Pad remainder of page with 0xFF (erased NAND state)
    if (page_buffer_offset < cfg->bytes_per_page) {
        memset(&page_buffer[page_buffer_offset], 0xFF, cfg->bytes_per_page - page_buffer_offset);
    }

    LOG_INF("Flushing page buffer: %u bytes at addr=%ld", page_buffer_offset, write_addr);

    // Write full page to flash
    int ret = nvs_write(write_addr, page_buffer, cfg->bytes_per_page);
    if (ret != 0) {
        LOG_ERR("Failed to flush page buffer: %d", ret);
        return ret;
    }

    // Update write address and reset buffer
    write_addr += cfg->bytes_per_page;
    page_buffer_offset = 0;

    return 0;
}


/*
 * nvs_log_record: logs a record to NVS with error checking and boundary validation
 */
int nvs_log_record(enum record_type type, const void *payload, uint16_t length, uint16_t dt_ticks)
{
    int ret;
    size_t total_size = sizeof(struct log_entry_hdr) + length;

    if (cfg == NULL) {
        return -ECANCELED;
    }

    // Validate inputs
    if (payload == NULL && length > 0) {
        LOG_ERR("Invalid payload pointer");
        return -EINVAL;
    }

    if (total_size > cfg->bytes_per_page) {
        LOG_ERR("Record too large: %zu bytes (max %u)", total_size, cfg->bytes_per_page);
        return -EINVAL;
    }

    // Calculate data region end (exclude META blocks)
    uint64_t data_region_end = (uint64_t)META_BLOCK_START * cfg->pages_per_block * cfg->bytes_per_page;

    // Check if write would exceed data region (accounting for current page)
    uint64_t next_page_addr = write_addr + cfg->bytes_per_page;
    if (next_page_addr > data_region_end) {
        LOG_ERR("Write would exceed data region (addr=%ld, limit=%llu)",
                write_addr, data_region_end);
        return -ENOSPC;
    }

    // Check if record fits in current page buffer
    if (page_buffer_offset + total_size > cfg->bytes_per_page) {
        // Flush current page to make room
        ret = nvs_flush_page_buffer();
        if (ret != 0) {
            return ret;
        }
    }

    // Build header
    struct log_entry_hdr hdr;
    hdr.record_type = type;
    hdr.length = length;
    hdr.dt_ticks = dt_ticks;

    // Copy header and payload into page buffer
    memcpy(&page_buffer[page_buffer_offset], &hdr, sizeof(hdr));
    page_buffer_offset += sizeof(hdr);

    memcpy(&page_buffer[page_buffer_offset], payload, length);
    page_buffer_offset += length;

    LOG_DBG("Buffered record: type=%d, len=%u, buf_offset=%u", type, length, page_buffer_offset);

    // Update metadata periodically
    static uint32_t record_count = 0;
    if (++record_count % 100 == 0) {
        LOG_INF(">>> Periodic metadata update triggered (record_count=%u)", record_count);
        // Flush buffer first to ensure write_addr is current
        ret = nvs_flush_page_buffer();
        if (ret == 0) {
            ret = nvs_write_metadata(write_addr);
            if (ret != 0) {
                LOG_WRN("Failed to update metadata (non-fatal): %d", ret);
            }
        }
    }

    return 0;
}







