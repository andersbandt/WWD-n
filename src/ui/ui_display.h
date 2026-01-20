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


/* My header files */
#include <clock.h>
#include <inv_imu_driver.h>


// Time field inversion enum
typedef enum {
    TIME_INVERT_NONE = 0,
    TIME_INVERT_HOURS = 1,
    TIME_INVERT_MINUTES = 2,
    TIME_INVERT_SECONDS = 3
} time_invert_field_t;


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
 * display_out_pedometer: displays pedometer step count
 */
void display_out_pedometer(int steps);


/**
 * display_out_temp: displays temperature reading
 */
void display_out_temp(int16_t temp);

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
