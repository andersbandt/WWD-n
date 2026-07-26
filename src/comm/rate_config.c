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

int rate_config_set_imu_odr_hz(uint16_t hz)
{
    int rc = imu_set_odr(hz);  // validates the discrete ODR steps itself

    if (rc != 0) {
        return rc;
    }

    imu_odr_hz = hz;
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
    return 0;
}

uint16_t rate_config_get_temp_interval_sec(void)
{
    return temp_interval_sec;
}
