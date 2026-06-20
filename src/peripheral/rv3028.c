//*****************************************************************************
//!
//! @file rv3028.c
//! @author Anders Bandt
//! @brief RV-3028-C7 hardware RTC — Zephyr RTC API wrapper
//! @version 1.0
//! @date 2026
//!
//! Hardware: I2C address 0x52, INT on P0.05, coin cell on VBACKUP.
//! DTS node: rv3028@52 in i2c0, compatible "microcrystal,rv3028".
//!
//! This is the preferred timekeeping source on real nRF52833 hardware because
//! it survives power cycles via the coin cell.  The counter-based rtc.c is
//! used during development on boards without the RV-3028.
//!
//! To activate: uncomment CONFIG_RTC / CONFIG_RTC_RV3028 in prj.conf, then
//! change get_current_time() in clock.c to call rv3028_get_time().
//*****************************************************************************

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>

#include <peripheral/rv3028.h>

LOG_MODULE_REGISTER(app_rv3028, LOG_LEVEL_INF);

static const struct device *rtc_dev;

void rv3028_init(void)
{
    rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rv3028));
    if (!device_is_ready(rtc_dev)) {
        LOG_ERR("RV-3028 not ready");
        rtc_dev = NULL;
    } else {
        LOG_INF("RV-3028 ready");
    }
}

Time rv3028_get_time(void)
{
    Time t = {0, 0, 0};

    if (rtc_dev == NULL) {
        return t;
    }

    struct rtc_time rt;
    if (rtc_get_time(rtc_dev, &rt) == 0) {
        t.hours   = (int8_t)rt.tm_hour;
        t.minutes = (int8_t)rt.tm_min;
        t.seconds = (int8_t)rt.tm_sec;
    } else {
        LOG_WRN("rtc_get_time failed");
    }
    return t;
}

void rv3028_set_time(Time t)
{
    if (rtc_dev == NULL) {
        return;
    }

    struct rtc_time rt = {
        .tm_hour  = t.hours,
        .tm_min   = t.minutes,
        .tm_sec   = t.seconds,
        .tm_mday  = 1,
        .tm_mon   = 0,
        .tm_year  = 126,   /* 2026 - 1900 */
        .tm_wday  = -1,
        .tm_yday  = -1,
        .tm_isdst = -1,
        .tm_nsec  = 0,
    };

    if (rtc_set_time(rtc_dev, &rt) != 0) {
        LOG_ERR("rtc_set_time failed");
    }
}

void rv3028_print_time(void)
{
    Time t = rv3028_get_time();
    LOG_INF("RV-3028 time: %02d:%02d:%02d", t.hours, t.minutes, t.seconds);
}
