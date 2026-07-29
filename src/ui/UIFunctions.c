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

/* UI and display */
#include <display.h>
#include <ui.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Time time_offset;


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
void system_prompt_for_time_UI_FUNC() {
    if (first_ui_time) {
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
    }

    // update display if we changed offset digit value
    if (btn_poll == 1 || btn_poll == 2) {
        display_out_time(time_offset, position == 0 ? TIME_INVERT_HOURS : position == 1 ? TIME_INVERT_MINUTES : TIME_INVERT_SECONDS); 
    }
        
    // ADVANCE (button 3 or 4)
    if (btn_poll == 8) {
        position++;
        display_out_time(time_offset, position == 0 ? TIME_INVERT_HOURS : position == 1 ? TIME_INVERT_MINUTES : TIME_INVERT_SECONDS);
        button_buffer_clear();
        if (position == 1) {
            clearAndPrintLine("MINUTES", 0, 12, FONT_LARGE);
        }
        else if (position == 2) {
            clearAndPrintLine("SECONDS", 0, 12, FONT_LARGE);
        }
        else if (position == 3) {
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



