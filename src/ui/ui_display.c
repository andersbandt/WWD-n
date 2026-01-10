//*****************************************************************************
//!
//! @file userInterface.c
//! @author Anders Bandt
//! @brief Provides user functionality through display
//! @version 1.0
//! @date September 2024
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* C99 header files */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>


/* My header files */
#include <display.h>
#include <clock.h>
#include <ui_display.h>



void clear_out_display() {
    clear_display();
}


/*
 *
 */
void display_out_bms(int charging, int battery_percent) {
    char text[24]; // only 21-22 chars possible in next line
    sprintf(text, "CHG[%d] BAT[%d]", charging, battery_percent);
    printLine(text, 0, 0, FONT_LARGE);  // print text displaying what kind of measurement it is
    return;
}


/*
 *
 */
void display_out_time(Time time) {
    char time_str[15];
    sprintf(time_str, "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);
    printLine(time_str, 2, 20, FONT_SMALL);
}


/*
 *
 */
void display_out_pedometer(int steps) {
    char text[15];
    sprintf(text, "Steps: %d", steps);
    printLine(text, 3, 30, FONT_LARGE);
}


/*
 *
 */
void display_out_temp(int16_t temp) {
    char text[14];
    sprintf(text, "Temp: %d", temp);
    printLine(text, 4, 12, FONT_LARGE);
}



void display_out_imu(int16_t * data) {
    char text1[10];
    char text2[10];
    char text3[10];

    sprintf(text1, "%d", data[0]);
    sprintf(text2, "%d", data[2]);
    sprintf(text3, "%d", data[3]);

    clear_display();
    printLine(text1, 0, 8, FONT_LARGE);  // print text displaying what kind of measurement it is
    printLine(text2, 1, 8, FONT_LARGE);  // print text displaying what kind of measurement it is
    printLine(text3, 2, 8, FONT_LARGE);  // print text displaying what kind of measurement it is
}


void display_out_statistics(int16_t * data, int num_data) {
    char text[30];
    text[0] = '|';

    // TODO: add some check on num_data length that is practical. I think I should just do 4 parameters for all the lines
    // TODO: I think num_data has to be a constant because my char abbove is?
    for (int i=0; i < num_data; i++) {
        sprintf(text, "%s%d,", text, data[i]);
    }
    clear_display();
    printToScreen(text, 1, 8, FONT_LARGE);  // print text displaying what kind of measurement it is
}


/*
 * displayMeasurement: displays a certain float measurement in the center of the screen
 */
void display_out_measurement(char * text, int value)
{
    char value_str[8];
    sprintf(value_str, "%d", value);

    clear_display();
    printToScreen(text, 2, 12, FONT_LARGE);  // print text displaying what kind of measurement it is
    printToScreen(value_str, 3, 12, FONT_LARGE);
    return;
}


void display_out_fault(int error_code)
{
    char text[12];
    sprintf(text, "Fault: [%d]", error_code);
    
    clear_display();
    printToScreen(text, 2, 12, FONT_LARGE);  // print text displaying what kind of measurement it is
    return;
}














