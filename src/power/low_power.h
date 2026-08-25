//*****************************************************************************
//! @file low_power.h
//! @brief Low-power mode policy: a user setting plus a battery-voltage trigger.
//*****************************************************************************
#ifndef LOW_POWER_H
#define LOW_POWER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Battery thresholds, in millivolts, with hysteresis so the mode does not
 * chatter around the trip point (battery voltage rises again as soon as a
 * heavy load like the backlight goes away, which is exactly what engaging
 * this mode causes).
 *
 * Measured brownout on this hardware is ~3.0 V, so 3.60 V leaves ~600 mV of
 * margin. See project power-profiling notes.
 */
#define LOW_POWER_ENTER_MV            3600
#define LOW_POWER_EXIT_MV             3900

/*
 * What the mode actually does. Deliberately ONLY display levers:
 *
 *  - Backlight cap. Measured: 100% -> 25% saves ~1.33 mA, and the curve is
 *    compressive (75% saves almost nothing), so 25% is near the knee.
 *  - Shorter display auto-off. The panel costs ~2.1 mA on its own even at 0%
 *    backlight, so turning it off sooner is the single biggest lever there is.
 *
 * NOT done here, on purpose:
 *  - BOOST_SEL / power_save_enable(). Measured, it selects the HIGHER 3.0 V
 *    rail and costs MORE power (+2.8..+14.6%), and holding it high arms a
 *    brownout latch: the board dies at ~3.0 V and will not restart until
 *    ~4.13 V. Asserting that on low battery would brick the device until it
 *    saw a charger. Never wire this mode to it.
 *  - Gyro off (0.34 mA) — would punch holes in the logged RECORD_IMU_FIFO
 *    gyro channel; a product decision, not a power one.
 *  - IMU ODR — measured near-useless, 800 Hz costs only 0.05 mA over 100 Hz.
 */
#define LOW_POWER_BACKLIGHT_MAX_PCT   25
#define LOW_POWER_TIMEOUT_MS          4000

/** @brief Initialise policy state. Call once, after power_init(). */
void low_power_init(void);

/** @brief Effective state: the user setting OR the battery trigger. */
bool low_power_is_active(void);

/** @brief The user's manual setting (menu toggle), independent of battery. */
bool low_power_user_enabled(void);

/** @brief Set the user's manual setting. Applies immediately. */
void low_power_set_user(bool on);

/** @brief True if the battery threshold (not the user) is forcing the mode. */
bool low_power_battery_engaged(void);

/**
 * @brief Feed a fresh battery reading, in millivolts. Applies hysteresis.
 *
 * Call periodically (the 9 s sensor thread already reads the battery).
 * Values <= 0 are ignored: battery_voltage_mv() returns 0 / negative when the
 * ADC or the MCP23008 divider leg is unavailable, and treating that as a flat
 * battery would engage the mode permanently on a board with no expander.
 */
void low_power_battery_update(int mv);

/**
 * @brief Clamp a requested backlight percentage to what the mode allows.
 *
 * Returns @p requested_pct unchanged when the mode is off.
 */
uint8_t low_power_cap_backlight(uint8_t requested_pct);

#endif /* LOW_POWER_H */
