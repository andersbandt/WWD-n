//*****************************************************************************
//!
//! @file wwd.c
//! @author Anders Bandt
//! @brief This file contains main function for WWD device
//! @version 0.9
//! @date September 2023
//!
//*****************************************************************************


#ifndef CLOCK_H
#define CLOCK_H


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>


typedef struct {
   int8_t hours;
   int8_t minutes;
   int8_t seconds;
} Time;


typedef enum {
    DIR_DOWN = 0,
    DIR_UP   = 1
} direction_t;



// set initial time offset
extern Time time_offset;


uint8_t increment_second(uint8_t s, direction_t dir);
uint8_t increment_minute(uint8_t minute, direction_t dir);
uint8_t increment_hour(uint8_t hour, direction_t dir);

/**
 * @brief 
 *
 */
uint32_t get_raw_ms();



uint32_t get_dt_ticks();


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
void print_time();


#endif

