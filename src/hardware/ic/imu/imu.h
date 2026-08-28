//*****************************************************************************
//!
//! @file imu.h
//! @author Anders Bandt
//! @brief Function definitions for IMU control. Will try to keep code IC independent
//! @version 0.9
//! @date November 2023
//!
//*****************************************************************************

#ifndef SRC_IC_IMU_IMU_H_
#define SRC_IC_IMU_IMU_H_

#include <stdint.h>
#include <stddef.h>

/* My header files  */
#include <circular_buffer.h>

/* Display driver selection */
// NOTE: these are done in the CMakeLists.txt file (USE_DERS_IMU and USE_ZEPHYR_IMU)

#ifdef USE_DERS_IMU
    #include <inv_imu_driver.h>
#endif


// FIFO configuration
#define IMU_FIFO_ENABLED     1
#define IMU_APEX_ENABLED     1
#define IMU_FIFO_WM          10

#define IMU_HIGH_RES_ENABLED 0   /* see imu_notes.md before enabling */


struct imu_sample {
    uint32_t step_count;
    int16_t temperature;
};


extern Circular_Buffer *imu_data_buffer;

extern uint32_t step_count;
extern int16_t imu_temperature;


/**
 * @brief function for initialization the IMU
 *
 * @return init status indictaor
 */
int imu_init();


/**
 * @brief function for starting accelerometer and gyrometer of IMU
 *
 * @return initialization status indicator
 */
int imu_start();


/**
 * @brief runtime ODR change for both accel and gyro, at the FSR imu_start()
 *        already configured them with (16 g / 2000 dps — not adjustable
 *        here, see startAccel()/startGyro() in ICM_42670.c for FSR control).
 *        Valid values: 25/50/100/200/400/800 Hz (ICM-42670 discrete steps).
 *
 * @return 0 on success, -EINVAL for an unsupported hz value
 */
int imu_set_odr(uint16_t odr_hz);


/**
 * @brief function for enabling APEX functionality
 */
int imu_apex();


/**
 * @brief records the most recent FIFO event for live UI display (does not
 * touch imu_data_buffer - see imu.c comment above latest_imu_event_mutex)
 */
void imu_set_latest_event(const inv_imu_sensor_event_t *evt);


/**
 * @brief returns the most recent FIFO event for live UI display, without
 * consuming anything from the NVS-bound imu_data_buffer queue. Returns
 * false (event zeroed) if no FIFO event has arrived yet.
 */
bool imu_get_latest_event(inv_imu_sensor_event_t *out);


/**
 * @brief processes from the IMU receive buffer
 */
void imu_process();


/**
 * @brief function for polling the register data of the IMU
 */
void imu_reg_poll();


/**
 * @brief function for just getting temperature data from IMU
 */
float imu_get_temp();


/**
 * @brief converts a raw IMU temperature register value to Fahrenheit - same
 * formula imu_get_temp() applies to a fresh reading, exposed so historical
 * raw values (e.g. from temp_history_get()) convert identically
 */
float imu_raw_to_fahrenheit(int16_t raw);


/**
 * @brief appends a raw temperature reading to the in-RAM recent-history ring
 * used by the temperature graph screen. See imu.c for why this is RAM-only
 * rather than reconstructed from the flash log.
 *
 * @param raw: raw register encoding, same as struct record_temperature.raw
 */
void temp_history_push(int16_t raw, int16_t soc_centi_c);

/**
 * @brief Recent IMU + MCU die temperatures, NEWEST FIRST.
 *
 * Counterpart to temp_history_get(), which returns oldest-first for the graph.
 * Both read the same ring; the two orders suit their two callers.
 *
 * @param out_imu raw IMU values, (raw/128)+25 = degrees C
 * @param out_soc MCU die, hundredths of a degree C; may be NULL
 * @param max_count capacity of the output arrays
 * @return how many samples were copied
 */
size_t temp_history_get_pairs(int16_t *out_imu, int16_t *out_soc, size_t max_count);


/**
 * @brief copies up to max_count of the most recent temp_history_push()
 * values into out, oldest first.
 *
 * @param[out] out: caller-owned buffer, at least max_count entries
 * @param max_count: capacity of out
 * @return number of samples copied (0 if none pushed yet)
 */
size_t temp_history_get(int16_t *out, size_t max_count);


/**
 * @brief counter incremented on every temp_history_push(), for cheap
 * "has anything changed since I last drew" checks by a UI screen that
 * redraws on a timer.
 */
uint32_t temp_history_get_rev(void);


/**
 * @brief Seconds of wall time that the newest `n` history samples span.
 *
 * For labelling a graph's time axis. Counts the gaps between samples plus the
 * age of the newest one, from the kernel clock rather than from an assumed
 * cadence, so a stalled producer widens the axis instead of hiding the stall.
 */
uint32_t temp_history_span_s(size_t n);


/* ---------------------------------------------------------------------------
 * Live gyro history — the IMU "Display readings" screen's time axis.
 *
 * The accel half of that screen needs no ring: one sample IS the orientation.
 * The gyro half does, because angular rate reads zero whenever the wrist is
 * still. See the ring's declaration in imu.c for why its decimation peak-holds
 * instead of averaging (and therefore why this is not a util/series.h ring).
 * ------------------------------------------------------------------------- */

/**
 * @brief appends one raw gyro sample. Called from event_cb() for every FIFO
 * sample; stores one peak-held value per GYRO_HISTORY_DECIMATE calls.
 */
void imu_gyro_history_feed(const int16_t gyro[3]);

/**
 * @brief the most recent stored samples, OLDEST FIRST, all three axes at once
 * so the traces cannot straddle a store and misalign.
 *
 * @param[out] x,y,z caller-owned buffers, at least max_count entries each
 * @return how many samples were copied (0 if none stored yet)
 */
size_t imu_gyro_history_get(int16_t *x, int16_t *y, int16_t *z, size_t max_count);

/** @brief bumped per stored sample — cheap "anything new to draw?" check. */
uint32_t imu_gyro_history_rev(void);

/** @brief seconds of wall time the newest `n` stored samples span. */
uint32_t imu_gyro_history_span_s(size_t n);


/**
 * @brief function for enabling the FIFO interrupt for the IMU
 */
int imu_fifo_interrupt();


/**
 * @brief retrieves the data from the FIFO
 */
void get_fifo_data();


/**
 * @brief prints out pedometer info from the IMU
 */
int imu_get_pedo();


/**
 * @brief raise-to-wake v1 (WOM only, no tilt confirm — see imu_notes.md
 * 2026-08-22): true if a WOM (motion) event fired on INT1 since the last
 * check. Always false when USE_DERS_IMU is not defined.
 */
bool imu_check_wom(void);


/**
 * @brief feeds one accel sample to the raise-to-wake gesture ring. Called from
 * event_cb() for every FIFO sample, on button_handler_thread. Single
 * writer/single reader with imu_check_raise_gesture(), so no locking.
 */
void imu_gesture_feed(const int16_t accel[3]);


/**
 * @brief True while the device is judged to be on a wrist.
 *
 * Drives whether RECORD_IMU_FIFO samples are written to NVS (imu_process()).
 * The FIFO is drained either way — this gates storage, not the sensor. Fails
 * safe to true: a false negative loses data, a false positive only costs
 * flash. See the wear-detection block in imu.c for the discriminator and the
 * (currently untuned) thresholds.
 */
bool imu_is_worn(void);


/**
 * @brief Consume-once wear-state change notification.
 *
 * Returns true exactly once per transition, so the caller can write a
 * RECORD_WEAR_STATE marker into the log. Single reader, on
 * button_handler_thread, same as the rest of the gesture ring.
 *
 * @param state Receives 1 for on-wrist, 0 for off-wrist. May be NULL.
 * @return true if a transition was pending (and has now been consumed).
 */
bool imu_wear_take_transition(uint8_t *state);


/**
 * @brief raise-to-wake v2: true exactly once per recognised wrist raise —
 * a WOM event followed by the display normal settling into a readable,
 * still pose. Call on every INT1 event from button_handler_thread.
 *
 * Consumes the WOM status internally, so do NOT also call imu_check_wom():
 * INT_STATUS2 is clear-on-read and the second reader would eat the event.
 * See the long comment on the implementation in imu.c for the rationale.
 */
bool imu_check_raise_gesture(void);


/**
 * @brief prints out pedometer info from the IMU
 */
int imu_log();


#endif /* SRC_IC_IMU_IMU_H_ */
