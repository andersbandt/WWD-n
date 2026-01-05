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


void display_out_bms(int charging, int battery_percent);

void display_out_time(Time time);


void display_out_pedometer(int steps);

void display_out_temp(int16_t temp);


/**
 *
 */
void display_out_statistics(int16_t * data, int num_data);



/**
 * @brief displays a certain measurement
 *
 * @param text     string representing accompanying text to display measurement with
 *
 * @param value    string representing value to represent
 *
 */
void display_out_measurement(char * text, int value);


void display_out_imu(int16_t * data);


void display_out_fault(int error_code);


/**
 * @brief displays text and waits for user to press select button to move forward
 *
 * @param text     string representing what text to display
 *
 */
//void displayAndWait(char * text);


/**
 * @brief prompts the user to select either OK or Cancel
 *
 * @returns a 1 if "OK" was selected, 0 otherwise
 */
/* int promptOkCancel(); */


/**
 * @brief prompts the user for a value from 1 to 100
 */
/* int promptFor1To100(char * prompt_string); */



#endif
