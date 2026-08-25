//*****************************************************************************
//! @file soc_temp.h
//! @brief nRF52833 on-die temperature sensor (the SoC's own TEMP peripheral).
//*****************************************************************************

#ifndef SRC_PERIPHERAL_SOC_TEMP_H_
#define SRC_PERIPHERAL_SOC_TEMP_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * This is the nRF52833's *own* die temperature, not ambient and not the IMU's.
 * It exists here specifically so there are two independent die sensors on one
 * board: the ICM-42670 reads ~14 degF warm and the datasheet rules out sensor
 * error (part-to-part offset is only +/-3 degC), so the leading explanation is
 * self-heating. One thermal probe cannot separate self-heat from sensor error.
 * Two, on opposite corners of the same PCB, can. See imu_notes.md's
 * "[RESEARCH 2026-08-23] Temperature reads ~14 degrees warm" entry.
 *
 * Cost is essentially nothing: the `temp` node (compatible "nordic,nrf-temp")
 * is already `status = "okay"` in NCS's nrf52833.dtsi, so this needs no DTS
 * change -- only CONFIG_TEMP_NRF5=y.
 *
 * Accuracy, from the nRF52833 datasheet: +/-5 degC over the full temperature
 * range with no calibration, 0.25 degC resolution. That is coarse in absolute
 * terms, which is fine -- the useful signal here is the *difference* between
 * this and the IMU, and how that difference tracks workload, not either
 * sensor's absolute reading.
 */

/**
 * @brief Binds the SoC TEMP device. Call once during init.
 * @return 0 on success, negative errno on failure (readings then unavailable).
 */
int soc_temp_init(void);

/** @brief True if soc_temp_init() bound the device successfully. */
bool soc_temp_available(void);

/**
 * @brief Takes a fresh die-temperature measurement.
 *
 * Blocking, but brief -- the TEMP peripheral's conversion is ~36 us and the
 * Zephyr driver waits on the DATARDY event.
 *
 * NOTE for when BLE lands: Nordic advises against taking a TEMP measurement
 * while the radio is active, because the peripheral shares analog resources
 * with it. Readings taken during a connection event may be off. Gate this on
 * radio inactivity, or accept the noise, once CONFIG_BT is enabled.
 *
 * @param out_centi_c  Receives the temperature in hundredths of a degree C
 *                     (e.g. 3125 = 31.25 degC). Untouched on failure.
 * @return 0 on success, negative errno on failure.
 */
int soc_temp_read_centi_c(int16_t *out_centi_c);

/** @brief Converts a centi-Celsius reading to Fahrenheit, for the UI. */
float soc_temp_centi_c_to_f(int16_t centi_c);

#endif /* SRC_PERIPHERAL_SOC_TEMP_H_ */
