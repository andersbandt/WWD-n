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


// IMU display mode enum
typedef enum {
    IMU_DISPLAY_ACCEL,
    IMU_DISPLAY_GYRO,
    IMU_DISPLAY_BOTH,
} imu_display_mode_t;



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

/**
 * display_out_battery: displays battery voltage (upper-right corner badge)
 *
 * @param mv: battery voltage in millivolts, as read from the divider (see
 * battery_voltage_mv() in power.c)
 */
void display_out_battery(int mv);

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
void display_out_data_stats(int write_offset, uint32_t meta_seq);


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
 * @brief displays IMU data (meant for a live-streaming type display)
 */
void display_out_imu(const inv_imu_sensor_event_t *event, imu_display_mode_t mode);


/**
 * @brief displays fault code on the screen
 *
 * @param error_code   integer representing the fault code to display
 */
void display_out_fault(int error_code);



#endif
