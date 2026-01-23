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
static off_t read_addr = 0;
static uint32_t metadata_seq = 0;  // Current metadata sequence number


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void nvs_init(void)
{
    mt29f_init(&cfg);

    // Calculate write offset from metadata
    addr_status = nvs_calc_offset();
    if (addr_status) {
        LOG_INF("NVS initialized: offset=%ld, seq=%u", write_addr, metadata_seq);

        // Log a reset marker to indicate system startup
        uint32_t marker = MAGIC_MARKER;
        int ret = nvs_log_record(RESET_MARKER, &marker, sizeof(marker), get_dt_ticks());
        if (ret != 0) {
            LOG_ERR("Failed to write reset marker: %d", ret);
        }
    } else {
        LOG_ERR("NVS offset calculation failed");
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
    LOG_INF("Erasing flash REGION... like the whole thing...\n");

    mt29f_chip_erase();

    // regenerate the address offset
    nvs_calc_offset();
    LOG_INF("\tnew write offset set at: [%d]", write_addr);
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

    // Populate metadata structure
    state.magic = MAGIC_MARKER;
    state.seq = metadata_seq++;
    state.nand_offset = offset;

    // Calculate CRC32 over magic, seq, and offset (exclude crc field itself)
    state.crc = crc32_ieee((uint8_t*)&state, sizeof(state) - sizeof(state.crc));

    // Use sequence number to determine which META block to write to (wear leveling)
    uint32_t meta_block_index = state.seq % META_BLOCK_COUNT;
    uint32_t meta_block = META_BLOCK_START + meta_block_index;

    // Calculate byte offset for this META block (first page of the block)
    off_t meta_addr = (off_t)meta_block * cfg.pages_per_block * cfg.bytes_per_page;

    LOG_DBG("Writing metadata: seq=%u, offset=%llu, block=%u, addr=%ld",
            state.seq, state.nand_offset, meta_block, meta_addr);

    // Write metadata to first page of the META block
    int ret = mt29f_write(meta_addr, &state, sizeof(state));
    if (ret != 0) {
        LOG_ERR("Failed to write metadata: %d", ret);
        return ret;
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

    // Scan all META blocks to find the one with highest valid sequence number
    for (int i = 0; i < META_BLOCK_COUNT; i++) {
        uint32_t meta_block = META_BLOCK_START + i;
        off_t meta_addr = (off_t)meta_block * cfg.pages_per_block * cfg.bytes_per_page;

        // Read metadata from this block
        int ret = mt29f_read(meta_addr, &state, sizeof(state));
        if (ret != 0) {
            LOG_WRN("Failed to read metadata from block %u", meta_block);
            continue;
        }

        // Check magic number
        if (state.magic != MAGIC_MARKER) {
            LOG_DBG("Block %u: invalid magic (0x%08X)", meta_block, state.magic);
            continue;
        }

        // Verify CRC
        uint32_t calculated_crc = crc32_ieee((uint8_t*)&state, sizeof(state) - sizeof(state.crc));
        if (calculated_crc != state.crc) {
            LOG_WRN("Block %u: CRC mismatch (calc=0x%08X, stored=0x%08X)",
                    meta_block, calculated_crc, state.crc);
            continue;
        }

        LOG_DBG("Block %u: valid metadata seq=%u, offset=%llu",
                meta_block, state.seq, state.nand_offset);

        // Keep track of the metadata with highest sequence number
        if (!found_valid || state.seq > best_state.seq) {
            best_state = state;
            found_valid = true;
        }
    }

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
 * nvs_log_record: logs a record to NVS with error checking and boundary validation
 */
int nvs_log_record(enum record_type type, const void *payload, uint16_t length, uint16_t dt_ticks)
{
    int ret;
    size_t total_size = sizeof(struct log_entry_hdr) + length;

    // Validate inputs
    if (payload == NULL && length > 0) {
        LOG_ERR("Invalid payload pointer");
        return -EINVAL;
    }

    if (length > cfg.bytes_per_page) {
        LOG_ERR("Record too large: %u bytes (max %u)", length, cfg.bytes_per_page);
        return -EINVAL;
    }

    // Calculate data region end (exclude META blocks)
    uint64_t data_region_end = (uint64_t)META_BLOCK_START * cfg.pages_per_block * cfg.bytes_per_page;

    // Check if write would exceed data region
    if ((uint64_t)write_addr + total_size > data_region_end) {
        LOG_ERR("Write would exceed data region (addr=%ld, size=%zu, limit=%llu)",
                write_addr, total_size, data_region_end);
        return -ENOSPC;
    }

    // Store original write address in case we need to rollback
    off_t original_addr = write_addr;

    // Write header
    struct log_entry_hdr hdr;
    hdr.record_type = type;
    hdr.length = length;
    hdr.dt_ticks = dt_ticks;

    ret = nvs_write_auto_offset(&hdr, sizeof(hdr));
    if (ret != 0) {
        LOG_ERR("Failed to write log header: %d", ret);
        write_addr = original_addr;  // Rollback
        return ret;
    }

    // Write payload
    ret = nvs_write_auto_offset((void*)payload, length);
    if (ret != 0) {
        LOG_ERR("Failed to write log payload: %d", ret);
        write_addr = original_addr;  // Rollback
        return ret;
    }

    // Update metadata every N records to persist write position
    // Using modulo to avoid excessive flash wear on META blocks
    static uint32_t record_count = 0;
    if (++record_count % 100 == 0) {  // Update metadata every 100 records
        ret = nvs_write_metadata(write_addr);
        if (ret != 0) {
            LOG_WRN("Failed to update metadata (non-fatal): %d", ret);
            // Don't fail the whole operation if metadata update fails
        }
    }

    LOG_DBG("Logged record: type=%d, len=%u, addr=%ld", type, length, original_addr);
    return 0;
}







