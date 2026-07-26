//*****************************************************************************
//!
//! @file rate_config.c
//! @author Anders Bandt
//! @brief Runtime-adjustable sample rates (see rate_config.h).
//! @version 0.1
//! @date 2026-07-26
//!
//*****************************************************************************

#include <errno.h>

#include <zephyr/logging/log.h>

#include "rate_config.h"
#include <imu.h>
#include <nvs.h>

LOG_MODULE_REGISTER(rate_config, CONFIG_LOG_DEFAULT_LEVEL);

#define DEFAULT_IMU_ODR_HZ        100
#define DEFAULT_TEMP_INTERVAL_SEC 10

static uint16_t imu_odr_hz = DEFAULT_IMU_ODR_HZ;
static uint16_t temp_interval_sec = DEFAULT_TEMP_INTERVAL_SEC;

void rate_config_init(void)
{
    imu_odr_hz = DEFAULT_IMU_ODR_HZ;
    temp_interval_sec = DEFAULT_TEMP_INTERVAL_SEC;
}

bool rate_config_load_persisted(void)
{
    uint16_t odr, interval;

    if (nvs_config_load(&odr, &interval) != 0) {
        LOG_INF("rate_config: no persisted config on flash, keeping defaults (odr=%u temp=%u)",
                imu_odr_hz, temp_interval_sec);
        return false;
    }

    int rc = imu_set_odr(odr);
    if (rc != 0) {
        LOG_WRN("rate_config: persisted ODR %u rejected by IMU (%d), keeping default %u",
                odr, rc, imu_odr_hz);
    } else {
        imu_odr_hz = odr;
    }

    temp_interval_sec = interval;
    LOG_INF("rate_config: restored from flash — odr=%u temp_interval=%u", imu_odr_hz, temp_interval_sec);
    return true;
}

int rate_config_set_imu_odr_hz(uint16_t hz)
{
    int rc = imu_set_odr(hz);  // validates the discrete ODR steps itself

    if (rc != 0) {
        return rc;
    }

    imu_odr_hz = hz;

    int save_rc = nvs_config_save(imu_odr_hz, temp_interval_sec);
    if (save_rc != 0) {
        LOG_WRN("rate_config: failed to persist ODR change: %d", save_rc);
    }

    return 0;
}

uint16_t rate_config_get_imu_odr_hz(void)
{
    return imu_odr_hz;
}

int rate_config_set_temp_interval_sec(uint16_t sec)
{
    if (sec == 0 || sec > 3600) {
        LOG_WRN("rate_config: rejected temp interval %u s (must be 1-3600)", sec);
        return -EINVAL;
    }

    temp_interval_sec = sec;
    LOG_INF("rate_config: temperature interval set to %u s", sec);

    int save_rc = nvs_config_save(imu_odr_hz, temp_interval_sec);
    if (save_rc != 0) {
        LOG_WRN("rate_config: failed to persist temp interval change: %d", save_rc);
    }

    return 0;
}

uint16_t rate_config_get_temp_interval_sec(void)
{
    return temp_interval_sec;
}
