//*****************************************************************************
//!
//! @file userInterface.c
//! @author Anders Bandt
//! @brief Provides user functionality through display
//! @version 1.0
//! @date September 2024
//!
//*****************************************************************************

#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* C99 header files */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   /* size_t, for display_out_activity() */


/* My header files */
#include <peripheral/clock.h>
#include <inv_imu_driver.h>


// Time field inversion enum
typedef enum {
    TIME_INVERT_NONE = 0,
    TIME_INVERT_HOURS = 1,
    TIME_INVERT_MINUTES = 2,
    TIME_INVERT_SECONDS = 3
} time_invert_field_t;


// Date field inversion enum — mirrors time_invert_field_t for the combined
// MM/DD/YYYY date-setting screen (see display_out_date()).
typedef enum {
    DATE_INVERT_NONE = 0,
    DATE_INVERT_MONTH = 1,
    DATE_INVERT_DAY = 2,
    DATE_INVERT_YEAR = 3
} date_invert_field_t;


/*
 * One frame of the live IMU screen. The two sensors arrive in different
 * shapes on purpose — the newest accel sample (an orientation) against a gyro
 * history (a movement) — see display_out_imu_live() in ui_display.c.
 */
struct imu_live_view {
    int16_t accel[3];        /* newest sample, raw counts */

    const int16_t *gyro_x;   /* history, oldest first, raw counts */
    const int16_t *gyro_y;
    const int16_t *gyro_z;
    size_t gyro_n;           /* samples per axis; < 2 skips the plot */

    const char *x_left;      /* time-axis labels, caller-formatted */
    const char *x_mid;
    const char *x_right;

    /* True on the first draw after entering the screen: paints the static
     * chrome (titles, legend) that every later frame leaves alone. */
    bool full_redraw;
};



/**
 * clear_out_display: abstracted function to clear the display
 */
void clear_out_display();


/**
 * display_out_bms: displays BMS charging and battery percentage
 */
void display_out_bms(int charging, int battery_percent);


/**
 * display_out_time: displays time with optional field inversion
 *
 * @param time: Time structure containing hours, minutes, seconds
 * @param invertField: Which field to invert (TIME_INVERT_NONE, TIME_INVERT_HOURS, TIME_INVERT_MINUTES, or TIME_INVERT_SECONDS)
 */
void display_out_time(Time time, time_invert_field_t invertField);

/**
 * display_clock_time_reset: force the next display_out_time() call to do a
 * full "HH:MM:SS" redraw (incl. colons) instead of a partial field update.
 * Call whenever the screen may have been cleared without display_out_time()'s
 * knowledge — e.g. switching back into UI_MODE_CLOCK.
 */
void display_clock_time_reset(void);


/**
 * display_out_date: displays a whole date as "MM/DD/YYYY" on one line, with
 * optional field inversion — the date half of the time/date setting screen
 * (system_prompt_for_time_UI_FUNC), which edits month, day and year together
 * rather than one per screen.
 *
 * @param date: Date structure containing day, month, year
 * @param invertField: which field to invert (DATE_INVERT_NONE/MONTH/DAY/YEAR)
 */
void display_out_date(Date date, date_invert_field_t invertField);


/**
 * display_out_pedometer: displays pedometer step count
 */
void display_out_pedometer(int steps);


/**
 * display_out_temp: displays temperature reading (lower-left corner badge)
 */
void display_out_temp(float temp);


/*
 * display_out_soc_temp: displays the nRF52833 die temperature (lower-left,
 * directly above the IMU temp badge). Rendered "M:<value> F"; the IMU badge
 * below it is "I:<value> F".
 * @param temp  degrees Fahrenheit
 */
void display_out_soc_temp(float temp);


/*
 * display_out_activity: activity session list (UI_MODE_ACTIVITY).
 *
 * Reads the catalogue and running state from activity.c directly rather than
 * taking them as parameters — see the note above the implementation.
 *
 * @param cursor       index of the highlighted activity
 * @param full_redraw  clear and repaint everything (screen was just entered
 *                     or cleared); otherwise only changed rows are touched
 */
void display_out_activity(size_t cursor, bool full_redraw);


/*
 * display_out_ble_indicator: "BT" badge on the clock face, accent when the
 * radio is enabled and dim when it is off — same convention as LP/CX.
 * @param on non-zero if BLE is enabled (ble_is_enabled())
 */
void display_out_ble_indicator(int on);


/*
 * display_out_wear_indicator: wear badge on the clock face, stacked directly
 * above the Bluetooth badge. Green check = on a wrist, red cross = off.
 *
 * @param worn non-zero if the watch is being worn (imu_is_worn())
 */
void display_out_wear_indicator(int worn);

/**
 * display_out_battery: displays battery voltage (upper-right corner badge)
 *
 * @param mv: battery voltage in millivolts, as read from the divider (see
 * battery_voltage_mv() in power.c)
 */
void display_out_battery(int mv);

/**
 * @brief displays the clock face's two power-status indicators ("LP" / "CHG")
 *
 * Both badges are always drawn — dim when inactive, accent-colored when
 * active — so their positions on the clock face never shift.
 *
 * @param charging: non-zero if the battery is charging (see battery_charging()
 *        in power.c — stubbed false until a charge-status signal is wired)
 * @param low_power: non-zero if the TPS63900 is in power-save mode
 *        (power_save_is_enabled(), power.c)
 */
void display_out_power_indicators(int charging, int low_power);


/**
 * @brief displays a certain measurement
 *
 * @param text     string representing accompanying text to display measurement with
 *
 * @param value    string representing value to represent
 *
 */
void display_out_measurement(char * text, int value);


/**
 * @brief displays NVS log stats (write offset and metadata sequence number)
 */
/* Data -> Log Stats screen. used_bytes is nvs_get_addr_offset(),
 * capacity_bytes is nvs_get_data_capacity() (0 if NVS never initialised).
 * full_redraw true only on first entry - see the comment in ui_display.c. */
void display_out_data_stats(uint64_t used_bytes, uint64_t capacity_bytes,
                            uint32_t meta_seq, bool full_redraw);


/**
 * @brief displays a stopwatch as MM:SS, with a RUNNING/PAUSED label
 */
void display_out_stopwatch(uint32_t elapsed_ms, bool running, bool full_redraw);


/**
 * display_out_statistics: displays statistical data
 *
 * @param data: pointer to array of int16_t data
 * @param num_data: number of data points in the array
 */
void display_out_statistics(const int16_t *data, size_t num_data);


/**
 * display_out_imu_live: the IMU "Display readings" screen.
 *
 * Accelerometer as three centre-zero bar gauges (the newest sample is the
 * orientation), gyroscope as three overlaid traces on a shared time axis
 * (angular rate only means something over time). Replaces display_out_imu(),
 * which printed three numbers and, in the only mode anything called it with,
 * showed AX/AY/GZ — two axes of one sensor and one of the other.
 */
void display_out_imu_live(const struct imu_live_view *v);


/**
 * display_out_brightness: the backlight brightness screen.
 *
 * Big percentage plus a fill bar. Replaces display_out_measurement() here
 * because that clears the whole panel on every change, which a held button
 * asking for a step every 120 ms cannot keep up with.
 *
 * @param pct         value actually in effect (already low-power capped)
 * @param full_redraw paint the static chrome; pass true only on entry
 */
void display_out_brightness(uint8_t pct, bool full_redraw);


/**
 * display_out_toggle: the screen for a binary setting.
 *
 * A green (on) or red (off) ring around the whole panel with the state in
 * words inside it, replacing the label-over-a-1-or-0 that
 * display_out_measurement() gave these screens. Every on/off setting uses
 * this so the encoding is learned once.
 *
 * @param label    what is being toggled, e.g. "Bluetooth"
 * @param on       current EFFECTIVE state — what is true, not what was asked
 * @param on_hint  button hint for enabling, or NULL
 * @param off_hint button hint for disabling, or NULL
 */
void display_out_toggle(const char *label, bool on, const char *on_hint,
                        const char *off_hint);


/**
 * @brief displays fault code on the screen
 *
 * @param error_code   integer representing the fault code to display
 */
void display_out_fault(int error_code);




/**
 * @brief Recent temperature samples, newest first: IMU die beside MCU die.
 *
 * Both columns because one die sensor cannot separate self-heating from
 * sensor error; the difference between them is the diagnostic.
 *
 * @param imu_raw   raw IMU values, newest first (temp_history_get_pairs())
 * @param soc_centi MCU die, hundredths of a degree C, same order
 * @param n         number of samples available; only as many as fit are drawn
 */
void display_out_temp_list(const int16_t *imu_raw, const int16_t *soc_centi, size_t n);


/**
 * @brief A screen stating in words that there is nothing to draw, and why.
 *
 * For empty/error states that would otherwise be rendered as a measurement --
 * a title over a big "0" reads as a real reading of zero, which is exactly how
 * the empty temperature graph disguised itself as working.
 *
 * @param title headline, drawn at FONT_MEDIUM
 * @param line1 first detail line, may be NULL
 * @param line2 second detail line, may be NULL
 */
void display_out_notice(const char *title, const char *line1, const char *line2);


/**
 * @brief Confirmation screen for the on-device flash erase.
 *
 * Cancel is first and pre-selected on purpose: the first SELECT after this
 * screen opens can never erase anything, and committing requires a different
 * button first. See the implementation comment.
 *
 * @param used_bytes how much log data is about to be destroyed
 * @param cursor 0 = Cancel, 1 = ERASE ALL
 * @param full_redraw true on entry; false repaints only the option rows
 */
void display_out_erase_confirm(uint64_t used_bytes, int cursor, bool full_redraw);

#endif
