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
 * true = power-save mode (lower VCC output voltage); false = normal (higher) output.
 */
void power_save_enable(bool enable);

/*
 * Profiling/debug only: hold VBAT_DIV_EN on/off continuously, bypassing
 * battery_voltage_mv()'s normal brief-pulse behavior. Used to measure the
 * divider's own static current draw in isolation. Not for normal runtime use.
 */
void power_debug_hold_vbat_div(bool on);

/*
 * Returns true if the battery is currently charging.
 * This board's BMS is a simple/discrete charge-management circuit (no I2C,
 * not a BQ25120A) — stubbed false unless/until there's a charge-status GPIO
 * worth wiring.
 */
bool battery_charging(void);

#endif /* SRC_POWER_POWER_H_ */
