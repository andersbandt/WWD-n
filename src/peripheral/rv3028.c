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
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include <peripheral/rv3028.h>

LOG_MODULE_REGISTER(app_rv3028, LOG_LEVEL_INF);

static const struct device *rtc_dev;

/* Raw I2C access for the UNIX time counter (regs 0x1B-0x1E) — not exposed by
 * Zephyr's RTC API, which only reads/writes the BCD seconds..year block. */
#define RV3028_I2C_ADDR       DT_REG_ADDR(DT_NODELABEL(rv3028))
#define RV3028_REG_UNIXTIME0  0x1B

static const struct device *i2c_dev;

/* Sakamoto's algorithm — 0 = Sunday, matching struct rtc_time's tm_wday.
 * The RV-3028 stores weekday as a raw byte written verbatim by the Zephyr
 * driver (it does not derive it from the date), and rtc_utils_validate_rtc_time()
 * rejects writes with an invalid weekday — so every set must supply one. */
static int day_of_week(uint16_t year, uint8_t month, uint8_t day)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year -= 1;
    }
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

void rv3028_init(void)
{
    rtc_dev = DEVICE_DT_GET(DT_NODELABEL(rv3028));
    if (!device_is_ready(rtc_dev)) {
        LOG_ERR("RV-3028 not ready");
        rtc_dev = NULL;
    } else {
        LOG_INF("RV-3028 ready");
    }

    i2c_dev = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(rv3028)));
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("RV-3028 I2C bus not ready");
        i2c_dev = NULL;
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

    /* Date/time live in one contiguous register block on the chip, so read
     * the current date first and re-write it unchanged alongside the new
     * time rather than clobbering it with a placeholder. */
    struct rtc_time rt;
    if (rtc_get_time(rtc_dev, &rt) != 0) {
        LOG_WRN("rv3028_set_time: could not read back existing date, defaulting to 2026-01-01");
        rt.tm_mday = 1;
        rt.tm_mon  = 0;
        rt.tm_year = 126;   /* 2026 - 1900 */
    }

    rt.tm_hour  = t.hours;
    rt.tm_min   = t.minutes;
    rt.tm_sec   = t.seconds;
    rt.tm_wday  = day_of_week(rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday);
    rt.tm_yday  = -1;
    rt.tm_isdst = -1;
    rt.tm_nsec  = 0;

    if (rtc_set_time(rtc_dev, &rt) != 0) {
        LOG_ERR("rtc_set_time failed");
    }
}

Date rv3028_get_date(void)
{
    Date d = {.day = 1, .month = 1, .year = 2026};

    if (rtc_dev == NULL) {
        return d;
    }

    struct rtc_time rt;
    if (rtc_get_time(rtc_dev, &rt) == 0) {
        d.day   = (uint8_t)rt.tm_mday;
        d.month = (uint8_t)(rt.tm_mon + 1);
        d.year  = (uint16_t)(rt.tm_year + 1900);
    } else {
        LOG_WRN("rtc_get_time failed");
    }
    return d;
}

void rv3028_set_date(Date d)
{
    if (rtc_dev == NULL) {
        return;
    }

    /* Same read-modify-write concern as rv3028_set_time(): preserve the
     * existing time-of-day instead of stomping it. */
    struct rtc_time rt;
    if (rtc_get_time(rtc_dev, &rt) != 0) {
        LOG_WRN("rv3028_set_date: could not read back existing time, defaulting to 00:00:00");
        rt.tm_hour = 0;
        rt.tm_min  = 0;
        rt.tm_sec  = 0;
    }

    rt.tm_mday  = d.day;
    rt.tm_mon   = d.month - 1;
    rt.tm_year  = d.year - 1900;
    rt.tm_wday  = day_of_week(d.year, d.month, d.day);
    rt.tm_yday  = -1;
    rt.tm_isdst = -1;
    rt.tm_nsec  = 0;

    if (rtc_set_time(rtc_dev, &rt) != 0) {
        LOG_ERR("rtc_set_time failed");
    }
}

uint32_t rv3028_get_unix_time(void)
{
    if (i2c_dev == NULL) {
        return 0;
    }

    uint8_t reg = RV3028_REG_UNIXTIME0;
    uint8_t buf[4];
    if (i2c_write_read(i2c_dev, RV3028_I2C_ADDR, &reg, 1, buf, sizeof(buf)) != 0) {
        LOG_WRN("rv3028_get_unix_time: I2C read failed");
        return 0;
    }

    /* Registers 0x1B..0x1E are little-endian: 0x1B = bits 7:0. */
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

void rv3028_set_unix_time(uint32_t t)
{
    if (i2c_dev == NULL) {
        return;
    }

    uint8_t buf[5] = {
        RV3028_REG_UNIXTIME0,
        (uint8_t)(t & 0xFF),
        (uint8_t)((t >> 8) & 0xFF),
        (uint8_t)((t >> 16) & 0xFF),
        (uint8_t)((t >> 24) & 0xFF),
    };

    if (i2c_write(i2c_dev, buf, sizeof(buf), RV3028_I2C_ADDR) != 0) {
        LOG_ERR("rv3028_set_unix_time: I2C write failed");
    }
}

void rv3028_print_time(void)
{
    Time t = rv3028_get_time();
    Date d = rv3028_get_date();
    LOG_INF("RV-3028: %04u-%02u-%02u %02d:%02d:%02d",
            d.year, d.month, d.day, t.hours, t.minutes, t.seconds);
}
