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
/* Cadence of the producer (sensor_update_thread's 9 s tick, TIMER1_PERIOD in
 * timer.c). Only used to label the graph's time axis; a mismatch would show
 * as a wrong axis, not as wrong data. */
#define TEMP_HISTORY_INTERVAL_S 9
static int16_t temp_history_buf[TEMP_HISTORY_LEN];
/* MCU die temperature, in hundredths of a degree C, captured on the SAME push
 * as the IMU value beside it. Kept in this ring rather than in a second one in
 * soc_temp.c specifically so the two can never drift out of alignment: one
 * ring, one head, one revision counter, one mutex, and a single producer that
 * has both numbers in hand. imu.c does not read the SoC sensor -- the value is
 * handed in. Pairing is the whole point of showing both (one die sensor cannot
 * separate self-heating from sensor error; two on one board can). */
static int16_t soc_history_buf[TEMP_HISTORY_LEN];
static size_t temp_history_head = 0;   /* next write index */
static size_t temp_history_count = 0;  /* valid entries so far, caps at TEMP_HISTORY_LEN */
static int64_t temp_history_last_ms = 0; /* uptime of the newest push, for the graph's
                                          * time axis - see temp_history_span_s() */
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
void temp_history_push(int16_t raw, int16_t soc_centi_c)
{
    k_mutex_lock(&temp_history_mutex, K_FOREVER);

    temp_history_buf[temp_history_head] = raw;
    soc_history_buf[temp_history_head]  = soc_centi_c;
    temp_history_head = (temp_history_head + 1) % TEMP_HISTORY_LEN;
    if (temp_history_count < TEMP_HISTORY_LEN) {
        temp_history_count++;
    }
    temp_history_rev++;
    temp_history_last_ms = k_uptime_get();

    k_mutex_unlock(&temp_history_mutex);
}


/*
 * temp_history_span_s: how many seconds of history `n` samples represent.
 *
 * (n-1) gaps at the producer's cadence, plus however long ago the newest
 * sample landed - the reader is looking at the plot now, not at the moment
 * the last sample was taken, and on a nine-minute axis that difference is
 * a visible fraction of the width.
 *
 * Measured against the clock rather than assumed from n * interval so that a
 * stall in the sensor thread stretches the axis instead of quietly compressing
 * real time into a shorter-looking window.
 */
uint32_t temp_history_span_s(size_t n)
{
    k_mutex_lock(&temp_history_mutex, K_FOREVER);

    if (n > temp_history_count) {
        n = temp_history_count;
    }

    uint32_t span = 0;
    if (n > 1) {
        span = (uint32_t)(n - 1) * TEMP_HISTORY_INTERVAL_S;
    }
    if (temp_history_last_ms > 0) {
        int64_t age_ms = k_uptime_get() - temp_history_last_ms;
        if (age_ms > 0) {
            span += (uint32_t)(age_ms / 1000);
        }
    }

    k_mutex_unlock(&temp_history_mutex);
    return span;
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
 * temp_history_get_pairs: like temp_history_get(), but copies the MCU die
 * temperature alongside each IMU sample and returns them NEWEST FIRST.
 *
 * Newest-first because the only caller is a scrolling list where the reader's
 * eye starts at the top and the most recent reading is the one they came to
 * see -- the opposite of temp_history_get(), which returns oldest-first so it
 * can be fed straight into drawGraph() left-to-right. Two orders on one ring
 * is deliberate; do not "unify" them without changing both callers.
 *
 * out_soc may be NULL if only the IMU side is wanted.
 */
size_t temp_history_get_pairs(int16_t *out_imu, int16_t *out_soc, size_t max_count)
{
    k_mutex_lock(&temp_history_mutex, K_FOREVER);

    size_t n = (temp_history_count < max_count) ? temp_history_count : max_count;

    for (size_t i = 0; i < n; i++) {
        /* head points at the NEXT write slot, so head-1 is the newest. */
        size_t idx = (temp_history_head + TEMP_HISTORY_LEN - 1 - i) % TEMP_HISTORY_LEN;
        out_imu[i] = temp_history_buf[idx];
        if (out_soc != NULL) {
            out_soc[i] = soc_history_buf[idx];
        }
    }

    k_mutex_unlock(&temp_history_mutex);
    return n;
}


/*
 * imu_get_pedo: refreshes the cached step_count from the IMU's APEX pedometer.
 *
 * getPedometer() reports the SENSOR's cumulative counter, and only writes
 * through to its out-param when the APEX step-detect status bit is set — i.e.
 * when the IMU has actually seen new steps since the last read. In between
 * (the common case: the device is sitting still, polled every 9 s by
 * sensor_update_thread) it leaves the out-param alone.
 *
 * This accumulates DELTAS of that sensor counter rather than adopting its
 * absolute value. The difference matters:
 *
 *   The old form was `if (count > step_count) step_count = count;` — accept
 *   any larger reading, reject any smaller one. Two ways that goes wrong, both
 *   silent and both permanent:
 *
 *   1. ONE bad large read is latched forever. SPI1 is shared with the MT29F
 *      and the ST7735S on this board, so a garbled APEX read is not
 *      hypothetical, and nothing in the old rule bounded how big a jump it
 *      would swallow. There was no path that could ever lower the total again.
 *      This is the shape of the 2304-steps-on-a-stationary-bench-board report:
 *      the count jumped once and then sat exactly still.
 *   2. If the sensor counter ever restarts (startApex() zeroes it), every
 *      later reading is smaller than the running total and is rejected, so the
 *      step count freezes at its old value and never moves again.
 *
 * Accumulating deltas fixes both. A bad reading costs at most one poll's worth
 * of steps and is never latched, and a sensor-side restart just resynchronises.
 *
 * The plausibility bound is deliberately generous — this is here to catch
 * corruption, not to second-guess the pedometer. Sprint cadence tops out around
 * 5 steps/s; PEDO_MAX_STEPS_PER_SEC is 6, plus a fixed slack so a late poll or
 * a burst straddling two reads is never clipped. Anything above that is not a
 * human walking, it is a bad read.
 *
 * Consequence worth knowing: there is deliberately no path here that resets the
 * total to 0. The clock face's midnight rollover is done by subtracting a daily
 * baseline in main.c, NOT by clearing this — the log and the BLE status keep
 * reporting the cumulative figure, from which a daily one can always be
 * derived. The reverse is not true.
 */
#define PEDO_MAX_STEPS_PER_SEC 6u
#define PEDO_JUMP_SLACK        20u   /* absorbs poll jitter and boundary bursts */

static uint32_t pedo_last_sensor_total;  /* last sensor cumulative we saw */
static int64_t  pedo_last_ms;            /* uptime at that reading */

int imu_get_pedo() {
    float step_cadence = 0;
    const char *activity = NULL; // set by getPedometer() to "unknown"/"walk"/"run" on a fresh step-detect event

    /* Seeded with the last sensor total, so a getPedometer() call that writes
     * nothing yields a delta of zero rather than a spurious jump. */
    uint32_t sensor_total = pedo_last_sensor_total;

    #ifdef USE_DERS_IMU
        volatile int status = getPedometer(&sensor_total, &step_cadence, &activity);
    #else
        volatile int status = 999;
    #endif

    if (status != 0) {
        /* Includes the ordinary "no step-detect event since last poll" case
         * (-11), and any transport error — in which case apex_data0 may hold
         * stack garbage, so nothing here may be trusted. */
        return step_count;
    }

    int64_t now = k_uptime_get();

    if (sensor_total >= pedo_last_sensor_total) {
        uint32_t delta     = sensor_total - pedo_last_sensor_total;
        uint32_t elapsed_s = (uint32_t)(((now - pedo_last_ms) + 999) / 1000);
        uint32_t max_steps = PEDO_MAX_STEPS_PER_SEC * elapsed_s + PEDO_JUMP_SLACK;

        if (delta <= max_steps) {
            step_count += delta;
        } else {
            LOG_WRN("pedometer: implausible jump of %u steps in %u s "
                    "(sensor total %u, cap %u) — dropped, not latched",
                    delta, elapsed_s, sensor_total, max_steps);
        }
    } else {
        /* Sensor counter went backwards. getPedometer() already treats a
         * backwards raw counter as a 16-bit wrap and adds 65536, so reaching
         * here means the whole cumulative figure fell — a restart or a bad
         * read, never a wrap. Resynchronise rather than accumulate anything. */
        LOG_WRN("pedometer: sensor total went backwards (%u -> %u) — resyncing",
                pedo_last_sensor_total, sensor_total);
    }

    /* Always resync, on every path, so one bad reading can never wedge the
     * comparison permanently the way the old accept-if-larger rule could. */
    pedo_last_sensor_total = sensor_total;
    pedo_last_ms           = now;

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
 * ---------------------------------------------------------------------------
 * Raise-to-wake v2: WOM trigger + firmware posture/settle gate
 * ---------------------------------------------------------------------------
 *
 * v1 woke the display on any WOM event. WOM is a per-axis acceleration
 * MAGNITUDE threshold (~312 mg here, OR'd across X/Y/Z), with no notion of
 * orientation or direction, so vigorous arm motion that has nothing to do with
 * looking at the watch — brushing teeth was the reported case — wakes it every
 * time. No threshold fixes that: a deliberate, gentle wrist raise generates
 * LESS acceleration than brushing does, so raising the bar to reject brushing
 * rejects the raise first.
 *
 * The ICM-42670-P has no raise-to-wake feature to fall back on (its APEX list
 * is Pedometer / Tilt / Low-g / Freefall / WoM / SMD), so the gesture is
 * recognised here in firmware instead.
 *
 * What we check, and why it is a posture test rather than trajectory matching:
 * the discriminator that actually separates "looking at the watch" from
 * "brushing teeth" is not the shape of the arc, it is that YOU CANNOT READ A
 * MOVING DISPLAY. A raise ends with the wrist parked in a readable
 * orientation; brushing is sustained oscillation that never parks. Matching
 * the arc itself would need a pre-motion reference orientation, continuous
 * sampling through the gesture, and per-user tuning, and it fails in the
 * direction that feels broken (missed wakes). Two cheap conditions, held
 * together, do the job:
 *
 *   1. ORIENTATION - the display normal (+Z, which reads +1 g with the watch
 *      face up) points meaningfully upward. Arm hanging at your side puts the
 *      normal roughly horizontal, az ~ 0; hand at your mouth is much the same.
 *   2. STILLNESS - peak-to-peak over the last RAISE_RING_LEN samples is small
 *      on all three axes. This is what kills brushing, sawing, clapping, and
 *      the mid-raise part of the gesture itself.
 *
 * Both must hold continuously for RAISE_SETTLE_MS, and the whole thing must
 * complete within RAISE_WINDOW_MS of the WOM event that armed it, so a slow
 * drift into a readable pose does not count as a raise.
 *
 * Deliberately NOT constrained: the X/Y (in-plane) components of gravity. A
 * roll limit would tighten this further, but it depends on how the PCB is
 * rotated inside the enclosure relative to the strap, which is not recorded
 * anywhere I can check — guessing a sign there would reject correct raises.
 * That is the first tuning hook to add once the mechanical orientation is
 * confirmed.
 *
 * Timing is driven off the existing INT1 FIFO traffic (~10 Hz at 100 Hz ODR /
 * watermark 10) — no new timer, no extra SPI. The accel runs continuously
 * whether or not the user is moving, so the state machine always has a clock.
 * The settle/window thresholds are in milliseconds via k_uptime_get() rather
 * than sample counts, so a runtime ODR change (rate_config.c) does not
 * silently retune the gesture.
 */

/* 16-bit accel at the configured +/-16 g FSR => 2048 LSB per g. */
#define ACCEL_LSB_PER_G     2048

#define RAISE_RING_LEN      16    /* ~160 ms of history at 100 Hz */
#define RAISE_AZ_MIN_LSB    1024  /* 0.5 g: display normal within ~60 deg of straight up */
#define RAISE_STILL_PP_LSB  512   /* 0.25 g peak-to-peak per axis over the ring */
#define RAISE_SETTLE_MS     250   /* how long orientation+stillness must hold */
#define RAISE_WINDOW_MS     1500  /* WOM -> settle must complete inside this */

static int16_t  raise_ring[RAISE_RING_LEN][3];
static uint8_t  raise_ring_count;   /* saturates at RAISE_RING_LEN */
static uint8_t  raise_ring_head;
static bool     raise_armed;
static int64_t  raise_armed_at;
static int64_t  raise_settle_start;
static bool     raise_left_readable = true;  /* must leave the readable pose before re-waking */

/* Defined further down with the rest of the wear-detection block; declared
 * here because imu_gesture_feed() below drives it. */
static void wear_evaluate(void);

/*
 * imu_gesture_feed: records one accel sample. Called from event_cb() for every
 * FIFO sample, on button_handler_thread — the same thread that later runs
 * imu_check_raise_gesture(), so the ring has a single reader and a single
 * writer and needs no lock.
 */
void imu_gesture_feed(const int16_t accel[3]) {
    raise_ring[raise_ring_head][0] = accel[0];
    raise_ring[raise_ring_head][1] = accel[1];
    raise_ring[raise_ring_head][2] = accel[2];

    raise_ring_head = (raise_ring_head + 1) % RAISE_RING_LEN;

    if (raise_ring_count < RAISE_RING_LEN) {
        raise_ring_count++;
    }

    /* Re-evaluate wear once per full window rather than per sample — see
     * wear_evaluate(). Wrapping to 0 means the ring just turned over. */
    if (raise_ring_head == 0) {
        wear_evaluate();
    }
}

/* True if the ring is full enough to judge, the display normal is pointing
 * up, and nothing is moving much. Integer only — no sqrt, no float. */
static bool raise_pose_is_readable(void) {
    if (raise_ring_count < RAISE_RING_LEN) {
        return false;  /* not enough history yet to call it still */
    }

    int16_t min[3] = { INT16_MAX, INT16_MAX, INT16_MAX };
    int16_t max[3] = { INT16_MIN, INT16_MIN, INT16_MIN };
    int32_t sum_z  = 0;

    for (unsigned i = 0; i < RAISE_RING_LEN; i++) {
        for (unsigned ax = 0; ax < 3; ax++) {
            if (raise_ring[i][ax] < min[ax]) { min[ax] = raise_ring[i][ax]; }
            if (raise_ring[i][ax] > max[ax]) { max[ax] = raise_ring[i][ax]; }
        }
        sum_z += raise_ring[i][2];
    }

    /* Stillness: every axis quiet. One noisy axis is enough to disqualify —
     * that is the whole point, brushing shows up on one or two axes. */
    for (unsigned ax = 0; ax < 3; ax++) {
        if ((int32_t)max[ax] - (int32_t)min[ax] > RAISE_STILL_PP_LSB) {
            return false;
        }
    }

    /* Orientation: mean Z over the window, so a single noisy sample can't
     * decide it. Signed compare — face-down is a large NEGATIVE az and must
     * not pass. */
    int32_t mean_z = sum_z / (int32_t)RAISE_RING_LEN;

    return mean_z >= RAISE_AZ_MIN_LSB;
}

/*
 * imu_check_raise_gesture: true exactly once per recognised wrist raise.
 *
 * Call on every INT1 event, from button_handler_thread only. Consumes the WOM
 * status internally (it is the arming trigger), so callers should NOT also
 * call imu_check_wom() — INT_STATUS2 is clear-on-read and the second reader
 * would eat the event.
 */
bool imu_check_raise_gesture(void) {
    int64_t now = k_uptime_get();
    bool    readable = raise_pose_is_readable();

    /* Re-arm interlock: after a wake, refuse to fire again until the wrist has
     * actually left the readable pose. Without this, the display going to
     * sleep while you are still holding your arm up would immediately re-wake
     * on the next stray WOM event and sit there flapping. */
    if (!readable) {
        raise_left_readable = true;
    }

    if (imu_check_wom()) {
        raise_armed        = true;
        raise_armed_at     = now;
        raise_settle_start = 0;
    }

    if (!raise_armed) {
        return false;
    }

    if (now - raise_armed_at > RAISE_WINDOW_MS) {
        raise_armed = false;  /* motion never resolved into a readable pose */
        return false;
    }

    if (!readable) {
        raise_settle_start = 0;  /* still moving, or wrong orientation */
        return false;
    }

    if (raise_settle_start == 0) {
        raise_settle_start = now;
        return false;
    }

    if (now - raise_settle_start < RAISE_SETTLE_MS) {
        return false;
    }

    raise_armed = false;

    if (!raise_left_readable) {
        return false;  /* never left the pose since the last wake */
    }

    raise_left_readable = false;
    return true;
}


/* ---------------------------------------------------------------------------
 * Wear detection: is the watch actually on a wrist?
 *
 * Purpose is NOT power — the FIFO is still drained either way, because the
 * raise-to-wake path and the pedometer both depend on it, and an undrained
 * FIFO overflows. What this gates is the NVS *write* (see imu_process()).
 * That matters because dump time, not flash capacity, is the real ceiling on
 * a multi-day capture: ~285 MB takes over an hour to pull over USB at the
 * measured 70 KB/s, and a meaningful fraction of any all-day log is a watch
 * lying on a desk. Dropping those stretches buys back capture days AND dump
 * minutes at once, and it removes exactly the data that pollutes any later
 * analysis.
 *
 * The discriminator is deliberately inverted from what you might reach for
 * first. Do not look for motion: a worn watch can be genuinely still (asleep,
 * hands on a desk) for long stretches, and gating on "motion seen recently"
 * would throw away sleep data, which is some of the most interesting data
 * there is. Look instead for the thing a WRIST never does:
 *
 *   a watch on a table is BOTH motionless AND fixed in orientation,
 *   continuously, for minutes.
 *
 * A wrist always drifts. Even asleep, an arm resettles; the mean gravity
 * vector wanders. A table does not. So the test is two-part and both parts
 * must hold for WEAR_OFF_AFTER_MS before we call it off-wrist:
 *
 *   1. STILLNESS  — per-axis peak-to-peak within the window is tiny.
 *   2. NO DRIFT   — the mean gravity vector has not moved from the reference
 *                   captured at the last motion event. This is the part that
 *                   does the real work over minutes; stillness alone is
 *                   satisfied by a sleeping wrist.
 *
 * Asymmetric by design: entering "not worn" takes minutes of evidence,
 * leaving it takes one window. A false "not worn" silently loses data; a
 * false "worn" only costs some flash. Bias hard toward logging.
 *
 * THRESHOLDS BELOW ARE UNTUNED — same status as raise-to-wake v2's. They are
 * reasoned from the +/-16 g scale (2048 LSB/g), not measured on a wrist.
 * Validate by wearing the device and checking that RECORD_WEAR_STATE
 * transitions in a dump line up with when it was actually taken off. Expect
 * WEAR_ORIENT_DELTA_LSB to be the one that needs adjusting.
 * ------------------------------------------------------------------------- */

/* 96 LSB ~= 47 mg peak-to-peak per axis. Well above the part's noise floor at
 * this FSR, well below anything a wrist produces. */
#define WEAR_STILL_PP_LSB      96

/* 128 LSB ~= 62 mg on an axis, i.e. roughly 3.6 degrees of tilt away from the
 * reference pose. Small enough that a wrist trips it within minutes, large
 * enough that thermal drift in the part does not. */
#define WEAR_ORIENT_DELTA_LSB  128

/* How long BOTH conditions must hold continuously before declaring off-wrist.
 * Three minutes is chosen to be longer than any plausible "holding very still"
 * episode while still catching a watch set down before a wash. */
#define WEAR_OFF_AFTER_MS      (3 * 60 * 1000)

static bool    wear_worn = true;        /* fail safe: assume worn until proven otherwise */
static int64_t wear_last_motion_ms;     /* 0 until the first evaluated window */
static int32_t wear_ref_mean[3];        /* mean gravity vector at the last motion */
static bool    wear_ref_valid;
static bool    wear_transition_pending;
static uint8_t wear_transition_state;

/*
 * wear_evaluate: one decision from a full ring window. Called from
 * imu_gesture_feed() each time the ring wraps (~6 Hz at 100 Hz ODR / 16
 * samples), not per sample — the min/max scan is 48 comparisons and there is
 * no value in running it at the full sample rate.
 *
 * Same single-threaded contract as the rest of this ring: button_handler_thread
 * only, so no locking.
 */
static void wear_evaluate(void)
{
    if (raise_ring_count < RAISE_RING_LEN) {
        return;  /* not enough history to judge */
    }

    int16_t min[3] = { INT16_MAX, INT16_MAX, INT16_MAX };
    int16_t max[3] = { INT16_MIN, INT16_MIN, INT16_MIN };
    int32_t sum[3] = { 0, 0, 0 };

    for (unsigned i = 0; i < RAISE_RING_LEN; i++) {
        for (unsigned ax = 0; ax < 3; ax++) {
            int16_t v = raise_ring[i][ax];

            if (v < min[ax]) { min[ax] = v; }
            if (v > max[ax]) { max[ax] = v; }
            sum[ax] += v;
        }
    }

    int32_t mean[3];
    bool    moving = false;

    for (unsigned ax = 0; ax < 3; ax++) {
        mean[ax] = sum[ax] / (int32_t)RAISE_RING_LEN;

        if ((int32_t)max[ax] - (int32_t)min[ax] > WEAR_STILL_PP_LSB) {
            moving = true;
        }
    }

    /* Drift against the pose held at the last motion event. Deliberately NOT
     * against the previous window — comparing to the previous window makes
     * arbitrarily slow drift invisible, since each step is below threshold. */
    if (wear_ref_valid && !moving) {
        for (unsigned ax = 0; ax < 3; ax++) {
            int32_t d = mean[ax] - wear_ref_mean[ax];

            if (d < 0) { d = -d; }
            if (d > WEAR_ORIENT_DELTA_LSB) {
                moving = true;
                break;
            }
        }
    }

    int64_t now = k_uptime_get();

    if (moving || !wear_ref_valid) {
        wear_last_motion_ms = now;
        wear_ref_mean[0] = mean[0];
        wear_ref_mean[1] = mean[1];
        wear_ref_mean[2] = mean[2];
        wear_ref_valid   = true;

        if (!wear_worn) {
            wear_worn = true;                 /* leave off-wrist on one window */
            wear_transition_state   = 1;
            wear_transition_pending = true;
            LOG_INF("wear: on-wrist");
        }
        return;
    }

    if (wear_worn && (now - wear_last_motion_ms) >= WEAR_OFF_AFTER_MS) {
        wear_worn = false;
        wear_transition_state   = 0;
        wear_transition_pending = true;
        LOG_INF("wear: off-wrist (still + no drift for %d s)",
                WEAR_OFF_AFTER_MS / 1000);
    }
}


bool imu_is_worn(void)
{
    return wear_worn;
}


bool imu_wear_take_transition(uint8_t *state)
{
    if (!wear_transition_pending) {
        return false;
    }

    wear_transition_pending = false;

    if (state != NULL) {
        *state = wear_transition_state;
    }
    return true;
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
        /* Emit the transition before the samples it explains, so a reader
         * hitting a gap in the log finds the reason immediately above it
         * rather than having to infer it. Logged regardless of wear state —
         * the off-wrist edge is exactly the one you need recorded. */
        uint8_t wear_state;
        if (imu_wear_take_transition(&wear_state)) {
            struct record_wear_state w = { .worn = wear_state };
            nvs_log_record(RECORD_WEAR_STATE, &w, sizeof(w), get_dt_ticks());
        }

        /* Gate only the WRITE, never the drain — the FIFO is still emptied
         * above whatever this decides. See the wear-detection block above for
         * why, and what an off-wrist gap costs if the detector is wrong. */
        if (imu_is_worn()) {
            nvs_log_record(RECORD_IMU_FIFO, &sample, sizeof(sample), get_dt_ticks());
        }
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




