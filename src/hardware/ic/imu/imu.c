//*****************************************************************************
//!
//! @file imu.c
//! @author Anders Bandt
//! @brief Contains functions for IMU control. Will try to keep code IC independent
//! @version 0.9
//! @date November 2023
//!
//*****************************************************************************


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
#include <string.h>
#include <errno.h>
    // below 2 are for printf only (I think)
    #include <stdio.h>
    #include <stddef.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* My header files  */
#include <peripheral/interrupt.h>
#include <circular_buffer.h>
#include <memory/nvs.h>
#include <peripheral/clock.h>

/* IMU header files*/
#include <imu.h>

#ifdef USE_DERS_IMU
    #include <ICM_42670.h>
    #include <imu_process.h>
    #include <inv_imu_driver.h>
#else
    #include <icm42670.h>
#endif


LOG_MODULE_REGISTER(imu, LOG_LEVEL_INF);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Circular_Buffer * imu_data_buffer = NULL;
uint32_t step_count;
int16_t imu_temperature = 0;

/* imu_data_buffer is drained by imu_process() (button_handler_thread, on
 * every FIFO watermark interrupt) into NVS. The UI's live "Display readings"
 * screen used to also call circular_buffer_remove() on this same buffer from
 * ui_refresh_thread - two unsynchronized consumers racing on the same
 * head/tail/count state, which corrupted `count` (size_t, so a lost
 * decrement race can wrap it to a huge value) and sent imu_process()'s
 * `while (!circular_buffer_empty(...))` into an effective infinite loop on
 * button_handler_thread, hanging the board. It also silently stole samples
 * away from the NVS log while the screen was open. Fixed by giving the UI
 * its own snapshot of the latest event instead of dequeuing from the
 * NVS-bound queue at all. */
static inv_imu_sensor_event_t latest_imu_event;
static bool latest_imu_event_valid = false;
K_MUTEX_DEFINE(latest_imu_event_mutex);

/* In-RAM recent-history ring for the temperature graph screen. Pushed
 * alongside nvs_log_record(RECORD_TEMPERATURE, ...) (see nvs_pipeline_tick()
 * in nvs_bringup.c) instead of read back from the flash log itself - the
 * flash log has no index, so reconstructing "last N temperature samples"
 * from it means scanning forward from offset 0 through every interleaved
 * record (mostly RECORD_IMU_FIFO, which vastly outnumbers RECORD_TEMPERATURE
 * at the default 100 Hz IMU / 10 s temp rates) - hundreds of NAND page reads
 * for even a modest sample count. This ring sidesteps that entirely: it's
 * live-only (reset on reboot, capped at TEMP_HISTORY_LEN samples), which is
 * what the graph screen actually wants. Guarded by its own mutex since
 * temp_history_push() runs on ui_refresh_thread outside display_draw_mutex's
 * scope (see handle_ui_input()/ui_refresh() in ui.c), while
 * temp_history_get() will run from whichever thread draws the graph. */
#define TEMP_HISTORY_LEN 60
static int16_t temp_history_buf[TEMP_HISTORY_LEN];
static size_t temp_history_head = 0;   /* next write index */
static size_t temp_history_count = 0;  /* valid entries so far, caps at TEMP_HISTORY_LEN */
static uint32_t temp_history_rev = 0;  /* bumped on every push - lets a redraw-on-change
                                         * UI screen skip re-plotting when nothing's new,
                                         * without an O(n) compare against the last draw */
K_MUTEX_DEFINE(temp_history_mutex);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*
 * imu_init: does register and data configuration for the IMU
 */
int imu_init() {
    LOG_INF("Initializing IMU ...");

    /* 64 slots: at 100 Hz with FIFO watermark of 50, we drain on every interrupt
     * and never accumulate more than ~50 events. 200 slots overflowed the 4KB heap
     * once accel_high_res/gyro_high_res were added to the event struct (6 bytes each). */
    imu_data_buffer = circular_buffer_init(64, sizeof(inv_imu_sensor_event_t));
    // LOG_INF("\nIMU data buffer setup");
    // LOG_INF("buffer = [%d]", imu_data_buffer->buffer);
    // LOG_INF("buffer_end = [%d]", imu_data_buffer->buffer_end);


    int rc = 0;
    #ifdef USE_DERS_IMU
        // do rough init
        rc = init_icm();
        if (rc != 0) {
            LOG_ERR("init_icm() failed: %d", rc);
            return rc;
        }

        // start IMU
        rc = imu_start();
        if (rc != 0) {
            LOG_ERR("imu_start() failed: %d", rc);
            return rc;
        }

        // actually configure and start IMU
        if (IMU_FIFO_ENABLED) {
            rc = imu_fifo_interrupt();
            if (rc != 0) {
                LOG_ERR("imu_fifo_interrupt() failed: %d", rc);
                return rc;
            }
        }
        if (IMU_APEX_ENABLED) {
            rc = imu_apex();
            if (rc != 0) {
                LOG_ERR("imu_apex() failed: %d", rc);
                return rc;
            }
        }

    #else
        rc |= icm42670_init();
        /* Set initial sample rates */
        rc |= icm42670_set_accel_rate(100); /* 100 Hz */
        rc |= icm42670_set_gyro_rate(100); /* 100 Hz */

        struct icm42670_data sensor_data;
        rc |= icm42670_read_all(&sensor_data);
        printf("Accel (m/s^2): X=%.2f, Y=%.2f, Z=%.2f\n",
            sensor_data.accel_x,
            sensor_data.accel_y,
            sensor_data.accel_z);

        printf("Gyro (dps): X=%.2f, Y=%.2f, Z=%.2f\n",
            sensor_data.gyro_x,
            sensor_data.gyro_y,
            sensor_data.gyro_z);
    #endif

    if (rc == 0) {
        LOG_INF("Initialized IMU\n");
    }
    else {
        LOG_ERR("Failed to initialize ICM42670 with code [%d]\n", rc);
    }

    return rc;
}
 

/*
 * imu_start: actually starts the accelerometer and gyroscope
 */
int imu_start() {
    int rc = 0;

    LOG_INF("Starting accel...");
    rc |= startAccel(100, 16);     // ODR=100 Hz, full-scale range=16

    LOG_INF("Starting gyro...");
    rc |= startGyro(100, 2000);    // ODR=100 Hz, full-scale range=2000 dps

    return rc;
}


/*
 * imu_set_odr: runtime ODR change for accel+gyro at the FSR imu_start() used.
 * startAccel()/startGyro() just write config registers on an already-running
 * sensor, so re-calling them at a new ODR is safe without a re-init.
 */
int imu_set_odr(uint16_t odr_hz) {
    switch (odr_hz) {
    case 25: case 50: case 100: case 200: case 400: case 800:
        break;
    default:
        LOG_ERR("imu_set_odr: unsupported %u Hz (want 25/50/100/200/400/800)", odr_hz);
        return -EINVAL;
    }

    int rc = 0;
    rc |= startAccel(odr_hz, 16);
    rc |= startGyro(odr_hz, 2000);

    if (rc != 0) {
        LOG_ERR("imu_set_odr(%u): failed: %d", odr_hz, rc);
        return rc;
    }

    LOG_INF("imu_set_odr: accel+gyro now at %u Hz", odr_hz);
    return 0;
}


/*
 * imu_apex: initializes APEX functionality
 */
int imu_apex() {
    int rc = 0;
    rc |= startApex();
    return rc;
}


/*
 * imu_fifo_interrupts: Enables the FIFO interrupt on the IMU
 */
int imu_fifo_interrupt() {
    LOG_INF("Enabling IMU interrupt for FIFO watermark level: %d", IMU_FIFO_WM);
    int rc = enableFifoInterrupt(IMU_FIFO_WM);
#if IMU_HIGH_RES_ENABLED
    rc |= inv_imu_enable_high_resolution_fifo(&icm_driver);
#endif
    return rc;
}



/*
 * imu_reg_poll: polls data and adds it to the circular buffer
 */
void imu_reg_poll() {
    inv_imu_sensor_event_t imu_event;

    int error = getDataFromIMUReg(&imu_event);
    if (error) {
        LOG_INF("\tgetDataFromIMUReg error: %d", error);
    }

//    event_print(&imu_event);

    /* ADD TO CIRCULAR BUFFER */
//    bool added = 0;
    // circular_buffer_add(imu_data_buffer, &imu_event);

//    if (!added) {
//        LOG_INF(display, 0, 0, "ERROR in adding to circular buffer. Probably full");
//    }
}


/*
 * get_fifo_data: reads data from the FIFO
 */
// TODO: really should document the flow. Where the event callback is stored, all the functions involved, circular buffer, etc
void get_fifo_data() {
    int fifo_status = getDataFromFifo();
}


/*
 * imu_set_latest_event: records the most recent FIFO event for live UI
 * display, without touching imu_data_buffer (the NVS-bound queue). Called
 * from event_cb() on button_handler_thread, same thread that later drains
 * imu_data_buffer via imu_process() - single writer, so the mutex here is
 * only guarding against the UI thread's concurrent read.
 */
void imu_set_latest_event(const inv_imu_sensor_event_t *evt) {
    k_mutex_lock(&latest_imu_event_mutex, K_FOREVER);
    latest_imu_event = *evt;
    latest_imu_event_valid = true;
    k_mutex_unlock(&latest_imu_event_mutex);
}


/*
 * imu_get_latest_event: returns the most recent FIFO event for live display.
 * Safe to call from any thread (e.g. ui_refresh_thread) - unlike the old
 * imu_deque(), this does not remove anything from imu_data_buffer, so it
 * can't race with imu_process()'s NVS drain or steal samples from the log.
 * Returns false (event left zeroed) if no FIFO event has arrived yet.
 */
bool imu_get_latest_event(inv_imu_sensor_event_t *out) {
    k_mutex_lock(&latest_imu_event_mutex, K_FOREVER);
    bool valid = latest_imu_event_valid;
    if (valid) {
        *out = latest_imu_event;
    }
    else {
        memset(out, 0, sizeof(*out));
    }
    k_mutex_unlock(&latest_imu_event_mutex);
    return valid;
}


/*
 * imu_get_temp: function to return temperature from IMU
 */
float imu_get_temp() {
    #ifdef USE_DERS_IMU
        int16_t imu_temp = getTempDataFromIMUReg();
    #else
        int16_t imu_temp = 100;
    #endif

    return imu_raw_to_fahrenheit(imu_temp);
}


/*
 * imu_raw_to_fahrenheit: shared raw-register-to-Fahrenheit conversion.
 * Factored out of imu_get_temp() so the temperature graph screen (which
 * converts historical raw values out of temp_history_get(), not a fresh
 * register read) uses the exact same formula instead of a second copy of it.
 *
 * Was truncating to int16_t twice (once via integer /128, once via the
 * int16_t return) — threw away all decimal precision. Float division
 * throughout keeps it.
 */
float imu_raw_to_fahrenheit(int16_t raw) {
    float temp_celsius = ((float)raw / 128.0f) + 25.0f;
    return temp_celsius * 1.8f + 32.0f;
}


/*
 * temp_history_push: appends a raw temperature reading (same encoding as
 * struct record_temperature.raw - (raw/128)+25 = degrees C) to the recent-
 * history ring, overwriting the oldest entry once full. See the comment on
 * temp_history_buf above for why this is RAM-only rather than flash-backed.
 */
void temp_history_push(int16_t raw)
{
    k_mutex_lock(&temp_history_mutex, K_FOREVER);

    temp_history_buf[temp_history_head] = raw;
    temp_history_head = (temp_history_head + 1) % TEMP_HISTORY_LEN;
    if (temp_history_count < TEMP_HISTORY_LEN) {
        temp_history_count++;
    }
    temp_history_rev++;

    k_mutex_unlock(&temp_history_mutex);
}


/*
 * temp_history_get_rev: returns a counter that increments every
 * temp_history_push(). Lets a caller that redraws on a timer (e.g. the
 * graph screen, redrawn every ui_refresh() tick) cheaply detect "nothing
 * new since I last drew" and skip the redraw, instead of re-plotting
 * unchanged data every tick or doing an O(n) compare against the last draw.
 */
uint32_t temp_history_get_rev(void)
{
    k_mutex_lock(&temp_history_mutex, K_FOREVER);
    uint32_t rev = temp_history_rev;
    k_mutex_unlock(&temp_history_mutex);
    return rev;
}


/*
 * temp_history_get: copies up to max_count of the most recent
 * temp_history_push() values into out, oldest first (so the caller can feed
 * it straight into drawGraph() left-to-right), and returns how many were
 * copied. Returns 0 (out untouched) if no samples have been pushed yet.
 */
size_t temp_history_get(int16_t *out, size_t max_count)
{
    k_mutex_lock(&temp_history_mutex, K_FOREVER);

    size_t n = (temp_history_count < max_count) ? temp_history_count : max_count;
    size_t oldest = (temp_history_head + TEMP_HISTORY_LEN - temp_history_count) % TEMP_HISTORY_LEN;
    size_t start = (oldest + (temp_history_count - n)) % TEMP_HISTORY_LEN;

    for (size_t i = 0; i < n; i++) {
        out[i] = temp_history_buf[(start + i) % TEMP_HISTORY_LEN];
    }

    k_mutex_unlock(&temp_history_mutex);
    return n;
}


/*
 * imu_get_pedo: refreshes the cached step_count from the IMU's APEX pedometer.
 *
 * getPedometer() only writes through to its out-param when the APEX step-detect
 * status bit is set - i.e. when the IMU has actually seen new steps since the
 * last read. In between (the common case: the device is sitting still, and this
 * is polled every 9s by sensor_update_thread) it leaves the out-param alone.
 * This function used to pass in a zeroed local and then assign it to step_count
 * unconditionally, so every poll without fresh step activity clobbered a
 * perfectly good running total back to 0 - which is what the clock face's
 * bottom-right badge was showing. The count would then "come back" as soon as
 * the user walked again, because the next detect interrupt refilled it with the
 * IMU's own cumulative total.
 *
 * Two guards, so a stale/absent reading can never destroy the total:
 *   1. `count` is seeded with the current step_count rather than 0, so a
 *      getPedometer() call that writes nothing is a no-op instead of a reset.
 *   2. The APEX step counter is cumulative (step_cnt plus the step_cnt_ovflw
 *      accumulator in ICM_42670.c), so it must never decrease - a lower reading
 *      is spurious and is rejected.
 *
 * Consequence worth knowing: there is deliberately no path here that resets the
 * total to 0. Nothing needs one today (the counter only restarts on reboot,
 * where step_count starts at 0 anyway); a future "reset steps" UI action would
 * have to clear step_count directly rather than expect a poll to do it.
 */
int imu_get_pedo() {
    float step_cadence = 0;
    const char *activity = NULL; // set by getPedometer() to "unknown"/"walk"/"run" on a fresh step-detect event

    uint32_t count = step_count;  // guard 1: unwritten out-param leaves the total intact

    #ifdef USE_DERS_IMU
        volatile int status = getPedometer(&count, &step_cadence, &activity);
    #else
        volatile int status = 999;
    #endif

    if (status == 0 && count > step_count) {  // guard 2: cumulative, so never accept a decrease
        step_count = count;
    }

    return step_count;
}


bool imu_check_wom(void) {
#ifdef USE_DERS_IMU
    return checkWom();
#else
    return false;
#endif
}


/*
 * imu_process: this function currently processes the circular buffers of raw data
 */
void imu_process() {
    inv_imu_sensor_event_t event;

    while (!circular_buffer_empty(imu_data_buffer)) {
        circular_buffer_remove(imu_data_buffer, &event);
        event_print(&event);

        struct record_imu_fifo sample = {
            .accel     = { event.accel[0], event.accel[1], event.accel[2] },
#if ICM_IS_GYRO_SUPPORTED
            .gyro      = { event.gyro[0],  event.gyro[1],  event.gyro[2]  },
#else
            .gyro      = { 0, 0, 0 },
#endif
            .timestamp = event.timestamp_fsync,
        };
#if NVS_LOG_IMU_SAMPLES
        nvs_log_record(RECORD_IMU_FIFO, &sample, sizeof(sample), get_dt_ticks());
#endif
    }
}


/*
 * imu_log: logs IMU data to NVS
 */
int imu_log(void) {
    int ret = 0;

    struct record_temperature temp = { .raw = imu_temperature };
    ret |= nvs_log_record(RECORD_TEMPERATURE, &temp, sizeof(temp), get_dt_ticks());

    struct record_step_count steps = { .steps = step_count };
    ret |= nvs_log_record(RECORD_STEP_COUNT, &steps, sizeof(steps), 0);

    return ret;
}




