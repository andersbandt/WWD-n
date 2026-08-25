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
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <mt29f_nand.h>


// META block configuration
#define META_BLOCK_COUNT    8

// CONFIG block configuration — persists user-adjustable rate settings (IMU
// ODR, temperature log interval) across power cycles. A separate small
// region from META_BLOCK_COUNT above: that region is exclusively the log
// write-offset metadata's rotation, keyed by struct log_state, and mixing a
// differently-shaped record into the same pages would break its scan. Placed
// immediately before the META blocks, shrinking the data region by
// CONFIG_BLOCK_COUNT blocks.
#define CONFIG_BLOCK_COUNT  2
#define CONFIG_MAGIC        0xC0F16000

// Set to 1 to enable logging of IMU FIFO samples to NVS, 0 to disable
#define NVS_LOG_IMU_SAMPLES 1



struct log_state {
    uint32_t magic;
    uint32_t seq;
    uint64_t nand_offset;
    uint32_t crc;
};


struct device_config_state {
    uint32_t magic;
    uint32_t seq;
    uint16_t imu_odr_hz;
    uint16_t temp_interval_sec;
    uint32_t crc;
} __packed;


enum record_type {
    /* infrastructure */
    TIME_ANCHOR,        /* wall-clock sync point, payload: struct record_time_anchor */
    RESET_MARKER,       /* device reset event,    payload: none (length=0) */

    /* sensor data */
    RECORD_IMU_FIFO,    /* raw accel+gyro FIFO sample, payload: struct record_imu_fifo */
    RECORD_TEMPERATURE, /* temperature reading,         payload: struct record_temperature */
    RECORD_STEP_COUNT,  /* pedometer snapshot,          payload: struct record_step_count */
    RECORD_POWER,       /* power mode / battery state,  payload: struct record_power */
    RECORD_SOC_TEMP,    /* nRF52833 die temperature,    payload: struct record_soc_temp */
    RECORD_WEAR_STATE,  /* on/off-wrist transition,     payload: struct record_wear_state */
    RECORD_ACTIVITY,    /* activity session marker,     payload: struct record_activity */

    /* APPEND ONLY. These values are written into every log record's header and
     * are hand-mirrored by dump_decoder.py in the companion wwd_gui_api repo
     * (no shared source of truth — see CLAUDE.md). Inserting or reordering
     * silently misdecodes every dump ever taken, including ones already on
     * disk. Adding at the end is always safe. */
};

#define MAGIC_MARKER 0xACACAC


struct log_entry_hdr {
    uint16_t record_type;  /* matches enum record_type */
    uint16_t length;       /* payload length in bytes  */
    uint16_t dt_ticks;     /* delta-ticks since last record */
} __packed;


/* ---- Per-record payload structs ---- */

/* TIME_ANCHOR: wall-clock sync point. raw_ticks is the full 32-bit kernel tick
 * count at capture — hdr.dt_ticks is only 16 bits and saturates, so decoders
 * must reconstruct time from the nearest anchor's raw_ticks, not by summing
 * deltas across anchor boundaries. time_valid=0 means the RTC was never set
 * (no backup battery / dummy time) and the wall-clock fields are placeholders. */
struct record_time_anchor {
    uint32_t raw_ticks;  /* sys_clock_tick_get() at capture, filled by nvs_log_time_anchor() */
    uint16_t year;
    uint8_t  month;      /* 1-12 */
    uint8_t  day;        /* 1-31 */
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
    uint8_t  time_valid; /* 0 = dummy/unset RTC, 1 = real wall clock */
} __packed;

/* RECORD_IMU_FIFO: one raw FIFO sample from the ICM-42670 */
struct record_imu_fifo {
    int16_t  accel[3];   /* X, Y, Z — raw ADC counts, apply sensitivity scale to convert */
    int16_t  gyro[3];    /* X, Y, Z — zero if ICM_IS_GYRO_SUPPORTED == 0 */
    uint16_t timestamp;  /* IMU internal counter, 16us/tick, rolls over every ~1.05s */
} __packed;

/* RECORD_TEMPERATURE: IMU die temperature */
struct record_temperature {
    int16_t raw;        /* raw register value — (raw / 128) + 25 = degrees C */
} __packed;

/* RECORD_STEP_COUNT: pedometer snapshot */
struct record_step_count {
    uint32_t steps;
} __packed;

/* RECORD_SOC_TEMP: nRF52833 on-die temperature, logged on the same cadence as
 * RECORD_TEMPERATURE so the two can be paired sample-for-sample.
 *
 * Stored in hundredths of a degree CELSIUS, unlike RECORD_TEMPERATURE's raw
 * IMU register value. That asymmetry is deliberate: the IMU's raw counts are
 * kept raw because the conversion constant is still being calibrated (the
 * datasheet's typical sensitivity is 126.9 LSB/degC, not the 128 the firmware
 * currently divides by), so re-deriving from raw later must stay possible.
 * The SoC reading has no such open question — Zephyr's driver already hands
 * back real engineering units, and re-encoding those into some raw form would
 * only lose information. */
struct record_soc_temp {
    int16_t centi_c;    /* hundredths of a degree C, e.g. 3125 = 31.25 degC */
} __packed;

/* RECORD_WEAR_STATE: on/off-wrist transition, written only on a CHANGE (not
 * periodically). A gap in RECORD_IMU_FIFO coverage should always be preceded
 * by one of these with worn=0 — if a gap has no marker, the cause was
 * something else (a reset, a dump pause, a full log) and should be
 * investigated rather than attributed to wear detection. */
struct record_wear_state {
    uint8_t worn;       /* 1 = on-wrist, 0 = off-wrist */
} __packed;

/* RECORD_ACTIVITY: an activity session boundary — the user declaring what
 * they are doing. Written only on a start or a stop, never periodically.
 *
 * Two things read this. A time-allocation report pairs START/STOP by
 * session_seq and sums the spans. Per-activity analysis (run form, say) uses
 * the pair as BOUNDS: every RECORD_IMU_FIFO between a START and its matching
 * STOP belongs to that session, so session_seq is effectively the run index.
 *
 * session_seq is unique only WITHIN A BOOT SEGMENT — it is RAM-only and
 * restarts at 0 after a reset. Scope it by the surrounding RESET_MARKER /
 * TIME_ANCHOR, which decoders already track for the time axis.
 *
 * nand_offset is where the marker itself lands, as a seek hint so analysis
 * can jump to a session instead of walking from zero. Advisory only: records
 * are buffered a page at a time, so it is exact to the page, and the marker's
 * position in the decoded stream stays the ground truth. */
struct record_activity {
    uint8_t  event;        /* 0 = stop, 1 = start */
    uint8_t  activity_id;  /* activity_id_t — see src/activity/activity.h */
    uint16_t session_seq;  /* pairs a START with its STOP; the run index */
    uint32_t nand_offset;  /* write offset at the marker (advisory) */
} __packed;

/* RECORD_POWER: power mode transition or periodic battery snapshot */
struct record_power {
    uint8_t  mode;          /* enum power_mode cast to uint8_t */
    uint16_t voltage_mv;    /* battery voltage in mV — 0 if not yet implemented */
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
void nvs_erase_chip();


/**
 * @brief calculates address offset
 *
 * @desc reads metadata from META blocks to find current write position
 */
bool nvs_calc_offset();


/**
 * @brief writes metadata to META blocks with wear leveling
 *
 * @param[in]   offset  current write offset to store
 * @return      0 on success, negative error code on failure
 */
int nvs_write_metadata(uint64_t offset);


/**
 * @brief reads metadata from META blocks
 *
 * @param[out]  offset  pointer to store recovered write offset
 * @return      0 on success, negative error code on failure
 */
int nvs_read_metadata(uint64_t *offset);


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
 *
 * dt_ticks is taken as 32-bit and saturated to 0xFFFF in the on-flash header —
 * get_dt_ticks() returns uint32_t and silently truncating it wrapped every 2 s
 * at 32768 ticks/s. A stored value of 0xFFFF therefore means "at least this".
 */
int nvs_log_record(enum record_type type, const void *payload, uint16_t length, uint32_t dt_ticks);


/**
 * @brief logs a TIME_ANCHOR record. Caller fills the wall-clock fields and
 * time_valid; raw_ticks is overwritten with the current kernel tick count.
 */
int nvs_log_time_anchor(struct record_time_anchor anchor);


/**
 * @brief size in bytes of the log region (whole chip minus the CONFIG and META
 *        blocks). Same byte-offset scheme as nvs_get_addr_offset(), so the two
 *        divide directly into a "how full is the log" fraction. 0 before
 *        nvs_init().
 */
uint64_t nvs_get_data_capacity(void);


/**
 * @brief getter for the current metadata sequence number (next seq to be written)
 */
uint32_t nvs_get_metadata_seq(void);


/**
 * @brief true if nvs_init() completed and the write offset is valid
 */
bool nvs_ready(void);


/**
 * @brief flushes erase_ log records to flash
 * @return 0 on success, negative error code on failure
 */
int nvs_flush_buffer(void);


/**
 * @brief dumps all committed NVS records from flash (offset 0 to write_addr) over LOG_INF.
 * In-memory page buffer content (not yet flushed) is not included.
 */
void nvs_dump(void);


/**
 * @brief persists user-adjustable rate settings to the dedicated CONFIG region
 * @return 0 on success, negative error code on failure (NVS not initialized, etc.)
 */
int nvs_config_save(uint16_t imu_odr_hz, uint16_t temp_interval_sec);


/**
 * @brief recovers rate settings written by nvs_config_save()
 * @param[out]  imu_odr_hz          filled in on success
 * @param[out]  temp_interval_sec   filled in on success
 * @return      0 if a valid config was found, -ENOENT if the region is blank/invalid,
 *              negative error code if NVS is not initialized
 */
int nvs_config_load(uint16_t *imu_odr_hz, uint16_t *temp_interval_sec);




#endif /* SRC_DATA_NVS_H_ */
