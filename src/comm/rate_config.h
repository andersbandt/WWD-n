//*****************************************************************************
//!
//! @file rate_config.h
//! @author Anders Bandt
//! @brief Runtime-adjustable sample rates, shared between the pipeline
//!        (main.c heartbeat + imu.c) and the host command protocol
//!        (src/comm/protocol.c CMD_GET_RATE/CMD_SET_RATE).
//! @version 0.1
//! @date 2026-07-26
//!
//*****************************************************************************

#ifndef SRC_COMM_RATE_CONFIG_H_
#define SRC_COMM_RATE_CONFIG_H_

#include <stdint.h>

// Call once at boot, before the pipeline starts. Resets both rates to their
// defaults (100 Hz IMU, 10 s temperature) — the values that were previously
// hardcoded as ODR args / TEMP_INTERVAL_SEC.
void rate_config_init(void);

// Valid values: 25/50/100/200/400/800 Hz (ICM-42670 discrete ODR steps —
// see accel_freq_to_param()/gyro_freq_to_param() in ICM_42670.c). Applies
// immediately via imu_set_odr(). Returns 0 on success, -EINVAL otherwise.
int rate_config_set_imu_odr_hz(uint16_t hz);
uint16_t rate_config_get_imu_odr_hz(void);

// Temperature record interval, in seconds. No hardware constraint — just
// how often main.c's pipeline logs a RECORD_TEMPERATURE — so any 1-3600 is
// accepted. Returns 0 on success, -EINVAL otherwise.
int rate_config_set_temp_interval_sec(uint16_t sec);
uint16_t rate_config_get_temp_interval_sec(void);

#endif /* SRC_COMM_RATE_CONFIG_H_ */
