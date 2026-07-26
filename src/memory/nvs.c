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
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/crc.h>

/* My header files  */
#include <nvs.h>
#include <mt29f_nand.h>
#include "../peripheral/clock.h"


LOG_MODULE_REGISTER(nvs, CONFIG_LOG_DEFAULT_LEVEL);


bool addr_status = false;
static off_t write_addr = 0;
// static off_t read_addr = 0;
static uint32_t metadata_seq = 0;  // Current metadata sequence number

// Flash configuration pointer (obtained from mt29f driver)
static const mt29f_cfg_t *cfg = NULL;

// Calculated flash parameters (initialized in nvs_init)
static uint32_t META_BLOCK_START = 0;
static uint32_t CONFIG_BLOCK_START = 0;
static uint64_t flash_size = 0;
static uint32_t config_seq = 0;  // Current config sequence number

// Page buffer for accumulating log records before writing to flash
static uint8_t page_buffer[2176];  // Match cfg->bytes_per_page
static uint16_t page_buffer_offset = 0;  // Current position in page buffer

/* Guards write_addr / page_buffer / page_buffer_offset / metadata_seq.
 * The pipeline (main.c heartbeat thread) calls nvs_log_record() every
 * second; src/comm/protocol.c's CMD_ERASE handler calls nvs_erase_chip() on
 * a separate thread on host request. Without this, an erase mid-flight can
 * reset write_addr under the pipeline thread's feet. mt29f_nand.c has its
 * own mutex for the SPI bus itself — this one is for the state layer above
 * it. Recursive-safe (k_mutex tracks lock count per owning thread), so the
 * internal nvs_log_record() -> nvs_flush_page_buffer() -> nvs_write_metadata()
 * nesting is fine. */
K_MUTEX_DEFINE(nvs_state_mutex);

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
    CONFIG_BLOCK_START = META_BLOCK_START - CONFIG_BLOCK_COUNT;
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
    k_mutex_lock(&nvs_state_mutex, K_FOREVER);
    int ret = nvs_flush_page_buffer();
    k_mutex_unlock(&nvs_state_mutex);
    return ret;
}


/*
 * nvs_erase_region: erases the whole NVS region
 */
void nvs_erase_chip() {
    LOG_INF("Erasing flash REGION... like the whole thing...\n");

    k_mutex_lock(&nvs_state_mutex, K_FOREVER);

    mt29f_chip_erase();

    // Wait for erase to fully complete
    k_msleep(100);

    // Reset page buffer and write address
    page_buffer_offset = 0;
    write_addr = 0;
    metadata_seq = 0;

    k_mutex_unlock(&nvs_state_mutex);

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
    int rc = mt29f_read(addr, buffer, len);
    return (rc != 0) ? rc : (int)len;
}


/*
 * nvs_write_metadata: writes metadata to META blocks with wear leveling
 */
static int nvs_write_metadata_impl(uint64_t offset)
{
    struct log_state state;

    LOG_DBG(">>> METADATA WRITE START: current_seq=%u, offset=%llu", metadata_seq, offset);

    // Populate metadata structure
    state.magic = MAGIC_MARKER;
    state.seq = metadata_seq++;
    state.nand_offset = offset;

    // Calculate CRC32 over magic, seq, and offset (exclude crc field itself)
    state.crc = crc32_ieee((uint8_t*)&state, offsetof(struct log_state, crc));

    LOG_DBG("CRC input bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
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
        LOG_DBG("Wrote metadata!");
    }

    return 0;
}

int nvs_write_metadata(uint64_t offset)
{
    k_mutex_lock(&nvs_state_mutex, K_FOREVER);
    int ret = nvs_write_metadata_impl(offset);
    k_mutex_unlock(&nvs_state_mutex);
    return ret;
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
 * nvs_config_write_impl: writes rate settings to the CONFIG region with
 * wear leveling, same magic+seq+CRC32 pattern as nvs_write_metadata_impl()
 * but rotated across its own CONFIG_BLOCK_COUNT blocks so it never collides
 * with the log write-offset metadata.
 */
static int nvs_config_write_impl(uint16_t imu_odr_hz, uint16_t temp_interval_sec)
{
    struct device_config_state state;

    state.magic = CONFIG_MAGIC;
    state.seq = config_seq++;
    state.imu_odr_hz = imu_odr_hz;
    state.temp_interval_sec = temp_interval_sec;
    state.crc = crc32_ieee((uint8_t*)&state, offsetof(struct device_config_state, crc));

    uint32_t total_config_pages = (uint32_t)CONFIG_BLOCK_COUNT * cfg->pages_per_block;
    uint32_t config_page_index  = state.seq % total_config_pages;
    uint32_t block_offset       = config_page_index / cfg->pages_per_block;
    uint32_t page_within_block  = config_page_index % cfg->pages_per_block;
    uint32_t config_block       = CONFIG_BLOCK_START + block_offset;
    off_t config_addr = (off_t)(config_block * cfg->pages_per_block + page_within_block)
                         * cfg->bytes_per_page;

    if (page_within_block == 0) {
        off_t block_addr = (off_t)config_block * cfg->pages_per_block * cfg->bytes_per_page;
        LOG_INF("Erasing config block %u before first write", config_block);
        mt29f_block_erase(block_addr);
    }

    uint8_t *page_buf = k_malloc(cfg->bytes_per_page);
    if (!page_buf) {
        LOG_ERR("Failed to allocate page buffer for config write");
        return -ENOMEM;
    }

    memset(page_buf, 0xFF, cfg->bytes_per_page);
    memcpy(page_buf, &state, sizeof(state));

    int ret = mt29f_write(config_addr, page_buf, cfg->bytes_per_page);
    k_free(page_buf);

    if (ret != 0) {
        LOG_ERR("Failed to write config: %d", ret);
        return ret;
    }

    LOG_INF("Config saved: odr=%u temp_interval=%u seq=%u block=%u page=%u",
            imu_odr_hz, temp_interval_sec, state.seq, config_block, page_within_block);
    return 0;
}

int nvs_config_save(uint16_t imu_odr_hz, uint16_t temp_interval_sec)
{
    if (cfg == NULL) {
        return -ECANCELED;
    }

    k_mutex_lock(&nvs_state_mutex, K_FOREVER);
    int ret = nvs_config_write_impl(imu_odr_hz, temp_interval_sec);
    k_mutex_unlock(&nvs_state_mutex);
    return ret;
}


/*
 * nvs_config_read_impl: recovers the latest valid rate settings, same
 * two-phase scan as nvs_read_metadata() but over CONFIG_BLOCK_COUNT blocks.
 */
static int nvs_config_read_impl(uint16_t *imu_odr_hz, uint16_t *temp_interval_sec)
{
    struct device_config_state state;
    struct device_config_state best_state = {0};
    bool found_valid = false;

    uint8_t *page_buf = k_malloc(cfg->bytes_per_page);
    if (!page_buf) {
        LOG_ERR("Failed to allocate page buffer for config read");
        return -ENOMEM;
    }

    int best_block_index = -1;
    for (int i = 0; i < CONFIG_BLOCK_COUNT; i++) {
        uint32_t config_block = CONFIG_BLOCK_START + i;
        off_t config_addr = (off_t)config_block * cfg->pages_per_block * cfg->bytes_per_page;

        if (mt29f_read(config_addr, page_buf, cfg->bytes_per_page) != 0) {
            LOG_WRN("Failed to read config from block %u", config_block);
            continue;
        }

        memcpy(&state, page_buf, sizeof(state));

        if (state.magic != CONFIG_MAGIC) {
            continue;
        }

        uint32_t calculated_crc = crc32_ieee((uint8_t*)&state, offsetof(struct device_config_state, crc));
        if (calculated_crc != state.crc) {
            LOG_WRN("Config block %u: CRC mismatch (calc=0x%08X, stored=0x%08X)",
                    config_block, calculated_crc, state.crc);
            continue;
        }

        if (!found_valid || state.seq > best_state.seq) {
            best_state = state;
            found_valid = true;
            best_block_index = i;
        }
    }

    if (found_valid) {
        uint32_t config_block = CONFIG_BLOCK_START + best_block_index;
        for (int p = 1; p < cfg->pages_per_block; p++) {
            off_t config_addr = (off_t)(config_block * cfg->pages_per_block + p)
                                 * cfg->bytes_per_page;

            if (mt29f_read(config_addr, page_buf, cfg->bytes_per_page) != 0) {
                break;
            }

            memcpy(&state, page_buf, sizeof(state));

            if (state.magic != CONFIG_MAGIC) {
                break;  // Unwritten page, stop
            }

            uint32_t calculated_crc = crc32_ieee((uint8_t*)&state, offsetof(struct device_config_state, crc));
            if (calculated_crc != state.crc) {
                break;
            }

            best_state = state;
        }
    }

    k_free(page_buf);

    if (!found_valid) {
        LOG_INF("No valid config found on flash");
        config_seq = 0;
        return -ENOENT;
    }

    *imu_odr_hz = best_state.imu_odr_hz;
    *temp_interval_sec = best_state.temp_interval_sec;
    config_seq = best_state.seq + 1;

    LOG_INF("Recovered config: odr=%u temp_interval=%u seq=%u",
            *imu_odr_hz, *temp_interval_sec, best_state.seq);
    return 0;
}

int nvs_config_load(uint16_t *imu_odr_hz, uint16_t *temp_interval_sec)
{
    if (cfg == NULL) {
        return -ECANCELED;
    }

    k_mutex_lock(&nvs_state_mutex, K_FOREVER);
    int ret = nvs_config_read_impl(imu_odr_hz, temp_interval_sec);
    k_mutex_unlock(&nvs_state_mutex);
    return ret;
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
 * nvs_get_metadata_seq: getter for the metadata sequence number.
 * This is the *next* seq to be written; the highest seq on flash is one less.
 */
uint32_t nvs_get_metadata_seq(void) {
    return metadata_seq;
}


/*
 * nvs_ready: true if nvs_init() completed and the write offset is valid
 */
bool nvs_ready(void) {
    return addr_status;
}


/*
 * nvs_log_time_anchor: logs a TIME_ANCHOR record. The caller supplies the
 * wall-clock fields (dummy is fine while the RTC is unset — mark time_valid=0);
 * raw_ticks is stamped here so anchor and dt_ticks share one timebase.
 */
int nvs_log_time_anchor(struct record_time_anchor anchor)
{
    anchor.raw_ticks = (uint32_t)sys_clock_tick_get();
    return nvs_log_record(TIME_ANCHOR, &anchor, sizeof(anchor), get_dt_ticks());
}


/*
 * nvs_dump: reads all committed records from flash (offset 0 to write_addr)
 * and prints them over LOG. Only flushed data is visible — in-memory page
 * buffer content is not included.
 */
void nvs_dump(void)
{
    if (cfg == NULL) {
        LOG_ERR("nvs_dump: NVS not initialized");
        return;
    }

    if (write_addr == 0) {
        LOG_INF("nvs_dump: nothing written yet");
        return;
    }

    LOG_INF("=== NVS DUMP: 0 to %ld ===", write_addr);

    uint8_t *page_buf = k_malloc(cfg->bytes_per_page);
    if (!page_buf) {
        LOG_ERR("nvs_dump: failed to allocate page buffer");
        return;
    }

    uint32_t record_count = 0;
    off_t addr = 0;

    while (addr < write_addr) {
        if (mt29f_read(addr, page_buf, cfg->bytes_per_page) != 0) {
            LOG_ERR("nvs_dump: read failed at addr=%ld", addr);
            break;
        }

        uint16_t page_offset = 0;

        while (page_offset + sizeof(struct log_entry_hdr) <= cfg->bytes_per_page) {
            if (page_buf[page_offset] == 0xFF) {
                break; /* rest of page is 0xFF padding */
            }

            struct log_entry_hdr hdr;
            memcpy(&hdr, &page_buf[page_offset], sizeof(hdr));
            page_offset += sizeof(hdr);

            if (page_offset + hdr.length > cfg->bytes_per_page) {
                LOG_WRN("nvs_dump: record overruns page at addr=%ld offset=%u", addr, page_offset);
                break;
            }

            const uint8_t *payload = &page_buf[page_offset];
            page_offset += hdr.length;
            record_count++;

            switch ((enum record_type)hdr.record_type) {
                case RECORD_IMU_FIFO: {
                    struct record_imu_fifo s;
                    memcpy(&s, payload, sizeof(s));
                    LOG_INF("[%u] IMU_FIFO  tmst=%u  dt=%u  ax=%d ay=%d az=%d  gx=%d gy=%d gz=%d",
                            record_count, s.timestamp, hdr.dt_ticks,
                            s.accel[0], s.accel[1], s.accel[2],
                            s.gyro[0],  s.gyro[1],  s.gyro[2]);
                    break;
                }
                case RECORD_TEMPERATURE: {
                    struct record_temperature t;
                    memcpy(&t, payload, sizeof(t));
                    LOG_INF("[%u] TEMPERATURE  dt=%u  raw=%d", record_count, hdr.dt_ticks, t.raw);
                    break;
                }
                case RECORD_STEP_COUNT: {
                    struct record_step_count sc;
                    memcpy(&sc, payload, sizeof(sc));
                    LOG_INF("[%u] STEP_COUNT  dt=%u  steps=%u", record_count, hdr.dt_ticks, sc.steps);
                    break;
                }
                case RECORD_POWER: {
                    struct record_power p;
                    memcpy(&p, payload, sizeof(p));
                    LOG_INF("[%u] POWER  dt=%u  mode=%u  voltage_mv=%u",
                            record_count, hdr.dt_ticks, p.mode, p.voltage_mv);
                    break;
                }
                case TIME_ANCHOR: {
                    struct record_time_anchor a;
                    if (hdr.length == sizeof(a)) {
                        memcpy(&a, payload, sizeof(a));
                        LOG_INF("[%u] TIME_ANCHOR  dt=%u  ticks=%u  %04u-%02u-%02u %02u:%02u:%02u %s",
                                record_count, hdr.dt_ticks, a.raw_ticks,
                                a.year, a.month, a.day, a.hours, a.minutes, a.seconds,
                                a.time_valid ? "(valid)" : "(RTC UNSET)");
                    } else {
                        LOG_INF("[%u] TIME_ANCHOR  dt=%u  len=%u (unknown layout)",
                                record_count, hdr.dt_ticks, hdr.length);
                    }
                    break;
                }
                case RESET_MARKER:
                    LOG_INF("[%u] RESET_MARKER  dt=%u", record_count, hdr.dt_ticks);
                    break;
                default:
                    LOG_WRN("[%u] UNKNOWN  type=%u  len=%u", record_count, hdr.record_type, hdr.length);
                    break;
            }

            /* Drain the log queue every 10 records to prevent buffer overflow.
             * log_process() flushes one pending message; loop until empty, then
             * sleep briefly to allow the UART backend to transmit. */
            if (record_count % 10 == 0) {
                while (log_process()) {}
                k_sleep(K_MSEC(10));
            }
        }

        addr += cfg->bytes_per_page;
    }

    k_free(page_buf);
    while (log_process()) {}
    k_sleep(K_MSEC(10));
    LOG_INF("=== NVS DUMP COMPLETE: %u records ===", record_count);
    while (log_process()) {}
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
static int nvs_log_record_impl(enum record_type type, const void *payload, uint16_t length, uint32_t dt_ticks)
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

    // Calculate data region end (exclude CONFIG and META blocks)
    uint64_t data_region_end = (uint64_t)CONFIG_BLOCK_START * cfg->pages_per_block * cfg->bytes_per_page;

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

    // Build header. dt_ticks saturates: the header field is 16-bit and
    // get_dt_ticks() is 32-bit — 0xFFFF on flash means "at least this many".
    // Absolute time comes from TIME_ANCHOR raw_ticks, not from summing deltas.
    struct log_entry_hdr hdr;
    hdr.record_type = type;
    hdr.length = length;
    hdr.dt_ticks = (dt_ticks > UINT16_MAX) ? UINT16_MAX : (uint16_t)dt_ticks;

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

int nvs_log_record(enum record_type type, const void *payload, uint16_t length, uint32_t dt_ticks)
{
    k_mutex_lock(&nvs_state_mutex, K_FOREVER);
    int ret = nvs_log_record_impl(type, payload, length, dt_ticks);
    k_mutex_unlock(&nvs_state_mutex);
    return ret;
}





