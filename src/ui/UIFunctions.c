//*****************************************************************************
//!
//! @file UIFunctions.c
//! @author Anders Bandt
//! @brief This file is for defining functions that get called from the user interface
//! @version 1.0
//! @date Feburary 2022
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* My header files */
#include <hardware/button.h>
#include <imu.h>
#include <peripheral/clock.h>
#include <memory/nvs.h>

/* UI and display */
#include <display.h>
#include <ui.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Time time_offset;
Date date_offset;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL VARIABLES -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int position = 0;  // position tracks where the cursor is - 0 for on the tens place, 1 for on the tenth place, 2 for on the done button
bool first_ui_time = true; // useful for doing things the first time a function has to get called

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void reset_uifunc_params() {
    position = 0;
    first_ui_time = true;
}


// TODO: need to really eliminate all sleep things in here BECAUSE THEY WILL FUCK UP THE ZEPHYR THREADS

/////////////////////////////////////////////////////
////////// MENU 0 - SYSTEM SETTINGS /////////////////
/////////////////////////////////////////////////////

/*
 * prompt_for_time: UI function to walk the user through prompting for time
 */
/*
 * system_prompt_for_time_UI_FUNC: walks HOURS -> MINUTES -> SECONDS -> DAY ->
 * MONTH -> YEAR, then commits the date (set_date() + ui_clock_set_date()) and
 * exits. NOTE: the time fields (time_offset) are edited here same as always
 * but were never wired to an actual clock-commit call — see clock_set_time()
 * TODO in clock.h / CLAUDE.md Known Issues. Only fixing the date half here.
 */
void system_prompt_for_time_UI_FUNC() {
    if (first_ui_time) {
        date_offset = current_date; // seed from the real date, not zeroed
        clearAndPrintLine("HOURS", 0, 12, FONT_LARGE);
        display_out_time(time_offset, TIME_INVERT_HOURS);
        button_buffer_clear();
        first_ui_time = false;
    }

    // Get button event from buffer instead of polling directly
    uint8_t btn_poll = get_button_event();

    // INCREMENT (button 1)
    if (btn_poll == 1) {
        if (position == 0) { // increment HOURS
            time_offset.hours = increment_hour(time_offset.hours, DIR_UP);
        }
        else if (position == 1) { // increment MINUTES
            time_offset.minutes = increment_minute(time_offset.minutes, DIR_UP);
        }
        else if (position == 2) { // increment SECONDS
            time_offset.seconds = increment_second(time_offset.seconds, DIR_UP);
        }
        else if (position == 3) { // increment DAY
            date_offset.day = increment_day(date_offset.day, date_offset.month, date_offset.year, DIR_UP);
        }
        else if (position == 4) { // increment MONTH
            date_offset.month = increment_month(date_offset.month, DIR_UP);
        }
        else if (position == 5) { // increment YEAR
            date_offset.year = increment_year(date_offset.year, DIR_UP);
        }
    }
    // DECREMENT (button 2)
    if (btn_poll == 2) {
        if (position == 0) { // increment HOURS
            time_offset.hours = increment_hour(time_offset.hours, DIR_DOWN);
        }
        else if (position == 1) { // increment MINUTES
            time_offset.minutes = increment_minute(time_offset.minutes, DIR_DOWN);
        }
        else if (position == 2) { // increment SECONDS
            time_offset.seconds = increment_second(time_offset.seconds, DIR_DOWN);
        }
        else if (position == 3) { // decrement DAY
            date_offset.day = increment_day(date_offset.day, date_offset.month, date_offset.year, DIR_DOWN);
        }
        else if (position == 4) { // decrement MONTH
            date_offset.month = increment_month(date_offset.month, DIR_DOWN);
        }
        else if (position == 5) { // decrement YEAR
            date_offset.year = increment_year(date_offset.year, DIR_DOWN);
        }
    }

    // update display if we changed offset digit value
    if (btn_poll == 1 || btn_poll == 2) {
        if (position <= 2) {
            display_out_time(time_offset, position == 0 ? TIME_INVERT_HOURS : position == 1 ? TIME_INVERT_MINUTES : TIME_INVERT_SECONDS);
        }
        else if (position == 3) {
            display_out_measurement("DAY", date_offset.day);
        }
        else if (position == 4) {
            display_out_measurement("MONTH", date_offset.month);
        }
        else if (position == 5) {
            display_out_measurement("YEAR", date_offset.year);
        }
    }

    // ADVANCE (button 3 or 4)
    if (btn_poll == 8) {
        position++;
        button_buffer_clear();

        if (position <= 2) {
            display_out_time(time_offset, position == 0 ? TIME_INVERT_HOURS : position == 1 ? TIME_INVERT_MINUTES : TIME_INVERT_SECONDS);
        }

        if (position == 1) {
            clearAndPrintLine("MINUTES", 0, 12, FONT_LARGE);
        }
        else if (position == 2) {
            clearAndPrintLine("SECONDS", 0, 12, FONT_LARGE);
        }
        else if (position == 3) {
            display_out_measurement("DAY", date_offset.day);
        }
        else if (position == 4) {
            display_out_measurement("MONTH", date_offset.month);
        }
        else if (position == 5) {
            display_out_measurement("YEAR", date_offset.year);
        }
        else if (position == 6) {
            set_date(date_offset);
            ui_clock_set_date(date_offset);
            ui_mode = UI_MODE_CLOCK;
        }
    }

    return;
}



void system_change_display_contrast_UI_FUNC() {
    // set up variables and print to screen
    uint8_t btn_poll;
    bool status = true;
    uint8_t brightness = 100;
    int new_brightness = 0;

    // add delay to prevent user from automatically exiting upon function entry
    display_out_measurement("Brightness", brightness);

    while (status) {
        k_usleep(10000);
        btn_poll = get_button_event();

        // decrement
        if (btn_poll == 1) {
            if (brightness >= 5) {
                brightness -= 5;
            }
            new_brightness = 1;
        }
        // increment
        else if (btn_poll == 2) {
            if (brightness <= 95) {
                brightness += 5;
            }
            new_brightness = 1;
        }
        // exit (both buttons)
        else if (btn_poll == 0) {
            status = false;
        }


        // adjust backlight brightness
        if (new_brightness) {
            display_out_measurement("Brightness", brightness);
            Backlight_Pct(brightness);
            new_brightness = 0;
        }
    }

    return;
}

void system_clear_faults_UI_FUNC(void) {
    // bq25120a_mask_faults();
}



/////////////////////////////////////////////////////
////////// MENU 1 - IMU SETTINGS   ///////// ////////
/////////////////////////////////////////////////////

void imuRead_UI_FUNC(void) {
    inv_imu_sensor_event_t event;
    event = imu_deque();
    display_out_imu(&event, IMU_DISPLAY_BOTH);
    return;
}


void imutempRead_UI_FUNC() {
    int16_t imu_temp = imu_get_temp();
    display_out_measurement("IMU temp", imu_temp);
    return;
}


void pedometer_UI_FUNC(void) {
    display_out_measurement("Steps", (int)step_count);
    return;
}


/////////////////////////////////////////////////////
////////// MENU 2 - DATA /////////////////////////////
/////////////////////////////////////////////////////

void data_stats_UI_FUNC(void) {
    if (!nvs_ready()) {
        display_out_measurement("NVS", -1);
        return;
    }

    display_out_data_stats(nvs_get_addr_offset(), nvs_get_metadata_seq());
    return;
}


/////////////////////////////////////////////////////
////////// MENU 3 - TIMER ////////////////////////////
/////////////////////////////////////////////////////

// stopwatch state - deliberately NOT touched by reset_uifunc_params() so
// elapsed time survives leaving/re-entering the screen; only SW4 clears it
static bool stopwatch_running = false;
static int64_t stopwatch_start_uptime;
static uint32_t stopwatch_elapsed_ms = 0;

void stopwatch_UI_FUNC(void) {
    uint8_t btn_poll = get_button_event();

    // START/PAUSE (button 1)
    if (btn_poll == BUTTON_1_MASK) {
        if (stopwatch_running) {
            stopwatch_elapsed_ms += (uint32_t)(k_uptime_get() - stopwatch_start_uptime);
            stopwatch_running = false;
        }
        else {
            stopwatch_start_uptime = k_uptime_get();
            stopwatch_running = true;
        }
    }
    // RESET (button 4)
    else if (btn_poll == BUTTON_4_MASK) {
        stopwatch_running = false;
        stopwatch_elapsed_ms = 0;
    }

    uint32_t elapsed = stopwatch_elapsed_ms;
    if (stopwatch_running) {
        elapsed += (uint32_t)(k_uptime_get() - stopwatch_start_uptime);
    }

    display_out_stopwatch(elapsed, stopwatch_running);
    return;
}


/* /\** */
/*  * tempRead_UI_FUNC: records the temp and displays it on the screen */
/*  *\/ */
/* void tempRead_UI_FUNC() */
/* { */
/*     // accompany text */
/*     char * text = "Temp (C): "; */

/*     // get value and turn into string */
/*     float temp = getTempValue(); */
/*     char pstring[6]; */
/*     floatToString(temp, &pstring, 5, 2); */

/*     // display measurement */
/*     displayMeasurement(text, &pstring); */
/* } */



