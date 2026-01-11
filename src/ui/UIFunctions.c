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
#include <clock.h>

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
bool first_ui_time = false; // useful for doing things the first time a function has to get called

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void reset_uifunc_params() {
    position = 0;
}


/////////////////////////////////////////////////////
////////// MENU 0 - SYSTEM SETTINGS /////////////////
/////////////////////////////////////////////////////

/*
 * prompt_for_time: UI function to walk the user through prompting for time
 */
void system_prompt_for_time_UI_FUNC() {
    if (first_ui_time) {
        position = 0;
        clearAndPrintLine("HOURS", 0, 12, FONT_LARGE);
        display_out_time(time_offset); // TODO: would be helpful to display current position INVERTED. Workaround is to print "HOURS", "MINUTES", "SECONDS"
    }


    uint8_t btn_poll = button_poll();

    // INCREMENT (button 1)
    if (btn_poll == 1) {
        if (position == 0) { // increment HOURS
            time_offset.hours = increment_hour(time_offset.hours);
        }
        else if (position == 1) { // increment MINUTES
            time_offset.minutes = increment_minute(time_offset.minutes);
        }
        else if (position == 2) { // increment SECONDS
            time_offset.seconds = increment_second(time_offset.seconds);
        }
    }
    // DECREMENT (button 2)
    if (btn_poll == 2) {
        // TODO: with expanded buttons lets add increment and decrement here
    }

    // update display if we changed offset digit value
    if (btn_poll == 1 || btn_poll == 2) {
        display_out_time(time_offset); 
        // TODO: would be helpful to display current position INVERTED. Workaround is to print "HOURS", "MINUTES", "SECONDS"
    }
        
    // ADVANCE (button 3 or 4)
    if (btn_poll == 8) {
        position++;
        if (position == 1) {
            clearAndPrintLine("MINUTES", 0, 12, FONT_LARGE);
        }
        else if (position == 2) {
            clearAndPrintLine("SECONDS", 0, 12, FONT_LARGE);
        }
    }

    return;
}



void system_change_display_contrast_UI_FUNC() {
    // set up variables and print to screen
    uint8_t btn_poll;
    bool status = true;
    uint8_t contrast = 50;
    int new_contrast;

    // add delay to prevent user from automatically exiting upon function entry
    display_out_measurement("Contrast", contrast);
    k_msleep(2000);

    while (status) {
        k_usleep(10000);
        btn_poll = button_poll();
        
        // decrement
        if (btn_poll == 1) {
            contrast--;
            new_contrast = 1;
            if (contrast < 0) {
                contrast = 0;
            }
        }
        // increment
        else if (btn_poll == 2) {
            contrast++;
            new_contrast = 1;
            if (contrast > 0xFF) {
                contrast = 0xFF;
            }
        }
        // exit (both buttons)
        else if (btn_poll == 0) {
            status = false;
        }


        // adjust contrast level
        if (new_contrast) {
            display_out_measurement("Contrast", contrast);
            changeContrast(contrast);
            new_contrast = 0;
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
    display_out_imu(event.accel);
    return;
}


void imutempRead_UI_FUNC() {
    int16_t imu_temp = imu_get_temp(NULL);
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



