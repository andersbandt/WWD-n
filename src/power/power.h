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
#include <stddef.h>
#include <stdint.h>

/* Must be called at startup before any other power function */
void power_init(void);

/*
 * Drive BOOST_SEL (MCP23008 GP6, the TPS63900 mode select) to the higher-rail
 * setting. Split out of power_init() because it must happen EARLY.
 *
 * The MCP23008 comes out of POR with every pin an input, so until something
 * drives GP6 the TPS63900's mode-select input floats and VCC is indeterminate.
 * power_init() runs late in main(), well after the RV-3028 bring-up — which
 * means the RTC was being probed on an undefined rail. A floating CMOS input
 * settles differently board to board, so this showed up as the RTC binding on
 * one board and not another with identical firmware (SN1 vs SN3, 2026-08-21).
 *
 * Idempotent: power_init() still calls this, so calling it early costs nothing.
 * Note it can only work once the expander itself answers — on boards where the
 * MCP23008 RESET/address rework has not been done, those pins float too and the
 * expander may not be present at all.
 */
void power_rail_init(void);

/*
 * Read battery voltage in millivolts.
 * Briefly enables the 1:2 voltage divider on AIN2 (P0.04) via VBAT_DIV_GPIO,
 * samples the SAADC, then disables the divider to save quiescent current.
 * Returns 0 on error.
 */
/*
 * Mirror of the most recent battery_voltage_mv() reading, for inspection over
 * SWD when no USB console is available. See the comment on the definition in
 * power.c for why this exists rather than calling the function from GDB.
 *
 * Read it with the core halted (nrfjprog --memrd, then --run), and use `seq`
 * to confirm a reading is fresh rather than left over from before the supply
 * was changed.
 */
struct battery_dbg_s {
    uint32_t seq;      /* increments on every completed reading */
    int16_t  raw_avg;  /* mean of `samples` raw SAADC counts */
    int16_t  raw_min;  /* spread of the burst — noise, at a glance */
    int16_t  raw_max;
    uint16_t samples;  /* conversions actually averaged */
    int32_t  mv_pin;   /* millivolts at AIN2 (divider output) */
    int32_t  mv_batt;  /* mv_pin * 2, i.e. what the function returns */
    int32_t  err;      /* 0, or the adc_read_dt() errno of the failed burst */
};

extern volatile struct battery_dbg_s battery_dbg;

int battery_voltage_mv(void);

/*
 * Convert battery millivolts to approximate state-of-charge (0-100%).
 * Based on a simple LiPo discharge curve (4200mV=100%, 3000mV=0%).
 */
uint8_t battery_percent(int mv);


/* ---- battery history (the Battery graph screen) --------------------------
 *
 * A rolling day of battery voltage, kept in RAM. Lives here rather than in
 * the UI because the producer is the sensor tick that already reads the
 * battery and the meaning is a property of the battery, not of a screen —
 * BLE or the log could read it too.
 *
 * 288 samples at one per 5 minutes is 24 h for 576 bytes. The 5-minute
 * cadence is decimation-with-averaging inside struct series (see series.h),
 * NOT a slower ADC read: the divider is still pulsed once per sensor tick as
 * before, and each stored point is the mean of its window. A discharge curve
 * is the one thing you want smoothed — per-reading ADC noise on this board is
 * worth several millivolts, which is the same order as an hour of real
 * discharge.
 */
#define BATTERY_HISTORY_LEN  288

/*
 * Feed one reading in, at the sensor tick's cadence. Takes the value the
 * caller already has rather than sampling again — battery_voltage_mv() pulses
 * VBAT_DIV_EN and costs energy per call.
 */
void battery_history_push(int mv);

/*
 * Copy up to max_count of the most recent stored samples, OLDEST FIRST (ready
 * for drawGraphEx). Values are millivolts. Returns how many were copied.
 */
size_t battery_history_get(int16_t *out, size_t max_count);

/* Bumped on every stored sample — lets the graph screen skip a redraw. */
uint32_t battery_history_rev(void);

/* How far back `n` stored samples reach, in seconds, for the time axis. */
uint32_t battery_history_span_s(size_t n);

/*
 * Control the TPS63900 buck-boost converter mode via BOOST_SEL (MCP23008 GP6).
 * true = power-save mode (lower VCC output voltage); false = normal (higher) output.
 */
void power_save_enable(bool enable);

/*
 * Returns true if power-save mode is currently selected on the TPS63900.
 *
 * Reports the shadow of what power_save_enable()/power_rail_init() last drove
 * onto BOOST_SEL, not a read-back of the pin — the expander pin is on I2C and
 * its level wouldn't tell you the converter's actual mode anyway. Drives the
 * clock face's "LP" indicator.
 */
bool power_save_is_enabled(void);

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
