//*****************************************************************************
//!
//! @file wwd.c
//! @author Anders Bandt
//! @brief This file contains main function for WWD device
//! @version 0.9
//! @date September 2023
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

#include <ti/display/Display.h>


uint8_t increment_second(uint8_t second);
uint8_t increment_minute(uint8_t minute);
uint8_t increment_hour(uint8_t hour);


typedef struct {
   uint8_t hours;
   uint8_t minutes;
   uint8_t seconds;
} Time;


// set initial time offset
// global variables
Time time_offset;

void init_time_offset();


/// functions

/**
 * @brief get system clock period
 *
 */
uint32_t get_clock_period();


/**
 * @brief 
 *
 */
uint32_t get_raw_ms();


/**
 * @brief 
 *
 */
uint32_t get_ms(void);


/**
 * @brief 
 *
 */
Time get_sys_time();


/**
 * @brief returns the current slope correction value
 */
uint32_t get_slope();


/**
 * @brief get current time
 *
 */
Time get_current_time();


/**
 * sets time offset at milliseconds running = 0
 */
void set_time_offset();


/**
 * @brief prints the current time
 *
 */
void print_time(Display_Handle display);


#endif

