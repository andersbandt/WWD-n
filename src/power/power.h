//*****************************************************************************
//!
//! @file power.h
//! @author Anders Bandt
//! @brief Battery voltage sensing and power control
//! @version 1.0
//! @date 2026
//!
//*****************************************************************************

#ifndef SRC_POWER_POWER_H_
#define SRC_POWER_POWER_H_

#include <stdbool.h>
#include <stdint.h>

/* Must be called at startup before any other power function */
void power_init(void);

/*
 * Read battery voltage in millivolts.
 * Briefly enables the 1:2 voltage divider on AIN2 (P0.04) via VBAT_DIV_GPIO,
 * samples the SAADC, then disables the divider to save quiescent current.
 * Returns 0 on error.
 */
int battery_voltage_mv(void);

/*
 * Convert battery millivolts to approximate state-of-charge (0-100%).
 * Based on a simple LiPo discharge curve (4200mV=100%, 3000mV=0%).
 */
uint8_t battery_percent(int mv);

/*
 * Control the TPS63900 buck-boost converter mode via BOOST_SEL (MCP23008 GP6).
 * true = boost mode enabled; false = normal buck mode.
 */
void boost_enable(bool on);

/*
 * Returns true if the battery is currently charging.
 * TODO: implement via BQ25120A Zephyr I2C driver (TI-SDK driver is incompatible).
 */
bool battery_charging(void);

#endif /* SRC_POWER_POWER_H_ */
