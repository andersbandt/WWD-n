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


// META block configuration
#define META_BLOCK_COUNT    8



struct log_state {
    uint32_t magic;
    uint32_t seq;
    uint64_t nand_offset;
    uint32_t crc;
};


enum record_type {
    /* infrastructure */
    TIME_ANCHOR,        /* wall-clock sync point, payload: struct record_time_anchor */
    RESET_MARKER,       /* device reset event,    payload: none (length=0) */

    /* sensor data */
    RECORD_IMU_FIFO,    /* raw accel+gyro FIFO sample, payload: struct record_imu_fifo */
    RECORD_TEMPERATURE, /* temperature reading,         payload: struct record_temperature */
    RECORD_STEP_COUNT,  /* pedometer snapshot,          payload: struct record_step_count */
    RECORD_POWER,       /* power mode / battery state,  payload: struct record_power */
};

#define MAGIC_MARKER 0xACACAC


struct log_entry_hdr {
    uint16_t record_type;  /* matches enum record_type */
    uint16_t length;       /* payload length in bytes  */
    uint16_t dt_ticks;     /* delta-ticks since last record */
} __packed;


/* ---- Per-record payload structs ---- */

/* RECORD_IMU_FIFO: one raw FIFO sample from the ICM-42670 */
struct record_imu_fifo {
    int16_t accel[3];   /* X, Y, Z — raw ADC counts, apply sensitivity scale to convert */
    int16_t gyro[3];    /* X, Y, Z — zero if ICM_IS_GYRO_SUPPORTED == 0 */
} __packed;

/* RECORD_TEMPERATURE: IMU die temperature */
struct record_temperature {
    int16_t raw;        /* raw register value — (raw / 128) + 25 = degrees C */
} __packed;

/* RECORD_STEP_COUNT: pedometer snapshot */
struct record_step_count {
    uint32_t steps;
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
 */
int nvs_log_record(enum record_type type, const void *payload, uint16_t length, uint16_t dt_ticks);


/**
 * @brief flushes erase_ log records to flash
 * @return 0 on success, negative error code on failure
 */
int nvs_flush_buffer(void);




#endif /* SRC_DATA_NVS_H_ */
