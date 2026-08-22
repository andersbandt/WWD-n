//*****************************************************************************
//!
//! @file rtc.c
//! @author Anders Bandt
//! @brief Hardware RTC driver for nRF52832/nRF52840 using Zephyr counter API
//! @version 0.1
//! @date March 2026
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 */
#include <stdint.h>

/* Zephyr */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/counter.h>

/* My headers */
#include <peripheral/rtc.h>
#include <peripheral/clock.h>   /* Time struct, used by rtc_get_time()/rtc_set_time() below */


LOG_MODULE_REGISTER(rtc, LOG_LEVEL_INF);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! DEFINES ---------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * Use RTC2.
 *   RTC0 — reserved for the BLE SoftDevice (even when BLE is not active,
 *           Nordic recommends leaving it alone for forward compatibility).
 *   RTC1 — used by the Zephyr kernel as the system timer source.
 *   RTC2 — free for application use.
 */
#define RTC_NODE DT_NODELABEL(rtc2)

/* Seconds in a day — the internal counter wraps at this value */
#define SECONDS_PER_DAY 86400U


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! STATIC VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const struct device *rtc_dev;

/*
 * Seconds since midnight. Updated from the counter alarm ISR context.
 * ARM Cortex-M4 guarantees atomic 32-bit aligned reads/writes, so a bare
 * volatile is sufficient here — no mutex needed for a single-writer ISR and
 * single-reader application thread, as long as callers only need a snapshot.
 */
static volatile uint32_t rtc_seconds = 0;

/* Tick frequency of the counter device (Hz). Set once during init. */
static uint32_t rtc_tick_hz = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL FUNCTIONS -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * second_tick_cb:
 *   Called by the counter driver every time the 1-second alarm fires.
 *   Re-arms itself as an absolute alarm so drift does not accumulate —
 *   the next target is always exactly rtc_tick_hz ticks after the last one,
 *   regardless of callback latency.
 */
static void second_tick_cb(const struct device *dev, uint8_t chan_id,
                            uint32_t ticks, void *user_data)
{
    rtc_seconds++;
    if (rtc_seconds >= SECONDS_PER_DAY) {
        rtc_seconds = 0;
        /* Date no longer advanced from here (2026-08-22): get_date()/set_date()
         * (clock.c) are now backed by the RV-3028 hardware RTC, which keeps its
         * own calendar current entirely in hardware - no software day-advance
         * needed, and this counter's "midnight" is time-since-boot, not real
         * midnight, so it would have been advancing the date at the wrong
         * moment anyway. This is also unreachable today regardless: nothing
         * calls rtc_init() to arm this alarm in the first place (rv3028_get_time()
         * is the live time source - see get_current_time() in clock.c). */
        // TODO: midnight rollover — reset daily variables here:
        //   - step_count (imu.h)
        //   - any other daily accumulators (calories, active minutes, etc.)
        // (Needs a real-midnight detector once this path is live again - e.g.
        // comparing rv3028_get_date() across polls - not this boot-relative
        // counter.)
    }

    uint32_t top = counter_get_top_value(dev);
    struct counter_alarm_cfg cfg = {
        .callback  = second_tick_cb,
        .ticks     = (ticks + rtc_tick_hz) % (top + 1),
        .user_data = NULL,
        .flags     = COUNTER_ALARM_CFG_ABSOLUTE,
    };

    int ret = counter_set_channel_alarm(dev, chan_id, &cfg);
    if (ret != 0) {
        LOG_ERR("Failed to re-arm RTC alarm: %d", ret);
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int rtc_init(void)
{
    rtc_dev = DEVICE_DT_GET(RTC_NODE);
    if (!device_is_ready(rtc_dev)) {
        LOG_ERR("RTC2 device not ready");
        return -ENODEV;
    }

    rtc_tick_hz = counter_get_frequency(rtc_dev);
    if (rtc_tick_hz == 0) {
        LOG_ERR("RTC2 reported 0 Hz frequency");
        return -EINVAL;
    }
    LOG_INF("RTC2 running at %u Hz", rtc_tick_hz);

    counter_start(rtc_dev);

    /* Read the current counter value and schedule the first 1-second alarm
     * as an absolute tick target to establish the no-drift baseline. */
    uint32_t now = 0;
    counter_get_value(rtc_dev, &now);

    struct counter_alarm_cfg cfg = {
        .callback  = second_tick_cb,
        .ticks     = now + rtc_tick_hz,
        .user_data = NULL,
        .flags     = COUNTER_ALARM_CFG_ABSOLUTE,
    };

    int ret = counter_set_channel_alarm(rtc_dev, 0, &cfg);
    if (ret != 0) {
        LOG_ERR("Failed to set initial RTC alarm: %d", ret);
        return ret;
    }

    LOG_INF("RTC initialised (time = 00:00:00)");
    return 0;
}


void rtc_set_time(Time t)
{
    /* Clamp inputs defensively */
    uint32_t h = (uint32_t)(t.hours   % 24);
    uint32_t m = (uint32_t)(t.minutes % 60);
    uint32_t s = (uint32_t)(t.seconds % 60);

    rtc_seconds = h * 3600U + m * 60U + s;

    LOG_INF("RTC time set to %02u:%02u:%02u", h, m, s);
}


Time rtc_get_time(void)
{
    uint32_t s = rtc_seconds;   /* atomic snapshot */

    return (Time){
        .hours   = (int8_t)(s / 3600U),
        .minutes = (int8_t)((s % 3600U) / 60U),
        .seconds = (int8_t)(s % 60U),
    };
}
