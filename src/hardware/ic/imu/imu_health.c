#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "imu_health.h"
#include "imu.h"
#include "imu_bringup.h"   /* imu_alive */
#include "nvs.h"
#include "rate_config.h"

#ifdef USE_DERS_IMU
#include "ICM_42670.h"
#include "inv_imu_regmap_rev_a.h"
#endif

LOG_MODULE_REGISTER(imu_health, CONFIG_LOG_DEFAULT_LEVEL);


/* Event codes for struct record_imu_health.event */
#define IMU_HEALTH_EVT_STALL           0
#define IMU_HEALTH_EVT_RECOVERED       1
#define IMU_HEALTH_EVT_RECOVERY_FAILED 2

/* PWR_MGMT0 could not be read. Not a value the register can hold in a healthy
 * part (ACCEL_LP_CLK_SEL + the reserved bits are never all set together), so
 * it can't be confused with a real reading in a dump. */
#define PWR_MGMT0_READ_FAILED 0xFF


static int16_t  last_raw;
static bool     have_last;
static uint16_t frozen_ticks;
static bool     stalled;

static uint32_t recovery_count;
static int64_t  last_recovery_ms;
static bool     have_recovered_once;


bool imu_health_is_stalled(void)
{
    return stalled;
}


uint32_t imu_health_recovery_count(void)
{
    return recovery_count;
}


/*
 * Detector C. Reads PWR_MGMT0 and reports the raw byte.
 *
 * This is the register that decides whether the accelerometer and gyroscope
 * are running at all, and it is the one the transport layer read-modify-writes
 * on every MREG access from two different threads with no lock. If a stall is
 * ever logged with ACCEL_MODE/GYRO_MODE reading OFF, that is the root cause
 * confirmed and the fix is to serialise those accesses. If they read LOW_NOISE
 * and the data still isn't moving, the part stalled internally and the answer
 * is the reset below regardless. Either way the next occurrence produces an
 * answer instead of another mystery.
 */
static uint8_t read_pwr_mgmt0(void)
{
#ifdef USE_DERS_IMU
    uint8_t reg = 0;

    if (getPwrMgmt0(&reg) != 0) {
        return PWR_MGMT0_READ_FAILED;
    }
    return reg;
#else
    return PWR_MGMT0_READ_FAILED;
#endif
}


static void log_health_record(uint8_t event, uint8_t pwr_mgmt0, int16_t raw_temp,
                              uint16_t ticks)
{
    struct record_imu_health rec = {
        .event        = event,
        .pwr_mgmt0    = pwr_mgmt0,
        .raw_temp     = raw_temp,
        .frozen_ticks = ticks,
    };

    /* dt_ticks of 0: this is an edge marker like RECORD_WEAR_STATE, not a
     * sample on the temperature cadence, so it carries no interval of its
     * own — its position in the stream is the timestamp. */
    nvs_log_record(RECORD_IMU_HEALTH, &rec, sizeof(rec), 0);
}


int imu_recover(void)
{
#ifdef USE_DERS_IMU
    int rc;

    imu_bus_lock();

    LOG_WRN("IMU recovery: soft-resetting and reconfiguring");

    /* init_icm() -> inv_imu_init() issues the documented soft reset
     * (SIGNAL_PATH_RESET.SOFT_RESET_DEVICE_CONFIG, 1 ms settle, serial
     * interface re-applied because the reset drops the 4-wire setting,
     * RESET_DONE verified) and then re-checks WHO_AM_I.
     *
     * Everything after it is imu_init()'s body minus the circular buffer
     * allocation — a soft reset returns every register to its default, so
     * accel/gyro, the FIFO watermark interrupt routing and APEX all have to be
     * rebuilt or the part comes back alive but mute. Deliberately NOT calling
     * imu_init() itself: that allocates imu_data_buffer, and doing so again
     * would leak the existing one on every recovery. */
    rc = init_icm();
    if (rc != 0) {
        LOG_ERR("IMU recovery: init_icm() failed (%d)", rc);
        goto out;
    }

    rc = imu_start();
    if (rc != 0) {
        LOG_ERR("IMU recovery: imu_start() failed (%d)", rc);
        goto out;
    }

    if (IMU_FIFO_ENABLED) {
        rc = imu_fifo_interrupt();
        if (rc != 0) {
            LOG_ERR("IMU recovery: imu_fifo_interrupt() failed (%d)", rc);
            goto out;
        }
    }

    if (IMU_APEX_ENABLED) {
        rc = imu_apex();
        if (rc != 0) {
            LOG_ERR("IMU recovery: imu_apex() failed (%d)", rc);
            goto out;
        }
    }

    /* imu_start() configures the compile-time default ODR. The live rate is
     * whatever CMD_SET_RATE last set (and rate_config restores from flash at
     * boot), so re-apply it or a recovery silently reverts the user's rate. */
    rc = imu_set_odr(rate_config_get_imu_odr_hz());
    if (rc != 0) {
        LOG_ERR("IMU recovery: imu_set_odr(%u) failed (%d)",
                rate_config_get_imu_odr_hz(), rc);
        goto out;
    }

    LOG_INF("IMU recovery: complete");

out:
    imu_bus_unlock();
    return rc;
#else
    return -ENOTSUP;
#endif
}


void imu_health_tick(int16_t raw_temp)
{
    if (!imu_alive) {
        /* Nothing to watch, and nothing to compare against when it comes up. */
        have_last    = false;
        frozen_ticks = 0;
        stalled      = false;
        return;
    }

    if (have_last && raw_temp == last_raw) {
        frozen_ticks++;
    } else {
        if (stalled) {
            LOG_INF("IMU data path is producing again (raw temp %d -> %d)",
                    last_raw, raw_temp);
        }
        frozen_ticks = 0;
        stalled      = false;
    }

    last_raw  = raw_temp;
    have_last = true;

    if (frozen_ticks < IMU_HEALTH_FROZEN_TICKS) {
        return;
    }

    uint8_t pwr_mgmt0 = read_pwr_mgmt0();

    if (!stalled) {
        stalled = true;
        LOG_ERR("IMU data path stalled: temp register frozen at raw %d for %u "
                "reads, PWR_MGMT0 = 0x%02x (accel_mode %u, gyro_mode %u)",
                raw_temp, frozen_ticks, pwr_mgmt0,
                pwr_mgmt0 & PWR_MGMT0_ACCEL_MODE_MASK,
                (pwr_mgmt0 & PWR_MGMT0_GYRO_MODE_MASK) >> PWR_MGMT0_GYRO_MODE_POS);
        log_health_record(IMU_HEALTH_EVT_STALL, pwr_mgmt0, raw_temp, frozen_ticks);
    }

    int64_t now = k_uptime_get();

    if (have_recovered_once &&
        (now - last_recovery_ms) < IMU_HEALTH_RECOVERY_MIN_INTERVAL_MS) {
        /* Still inside the cooldown from the last attempt. Stay stalled and
         * say nothing more — one stall record per episode is the diagnostic,
         * repeating it every 54 s is just noise. */
        return;
    }

    last_recovery_ms    = now;
    have_recovered_once = true;
    recovery_count++;

    int rc = imu_recover();

    log_health_record(rc == 0 ? IMU_HEALTH_EVT_RECOVERED
                              : IMU_HEALTH_EVT_RECOVERY_FAILED,
                      read_pwr_mgmt0(), raw_temp, frozen_ticks);

    /* Re-arm the detector either way. If the reset worked, the next reading is
     * a fresh conversion and the counter starts clean; if it didn't, the count
     * builds again and the cooldown above decides when to retry. */
    frozen_ticks = 0;
    have_last    = false;
}
