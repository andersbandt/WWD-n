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
#include <stdbool.h>


typedef struct {
   int8_t hours;
   int8_t minutes;
   int8_t seconds;
} Time;


typedef struct {
    uint8_t  day;    /* 1–31  */
    uint8_t  month;  /* 1–12  */
    uint16_t year;   /* e.g. 2026 */
} Date;


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


/* ---- Date ---- */

/** Returns true if year is a leap year. */
bool is_leap_year(uint16_t year);

/** Returns the number of days in a given month, accounting for leap years. */
uint8_t days_in_month(uint8_t month, uint16_t year);

/**
 * @brief Set the current calendar date.
 *
 * Backed by the RV-3028 hardware RTC (rv3028_set_date()) — persists across
 * reboots via the chip's coin cell, same as get_current_time()/
 * set_time_offset() are backed by rv3028_get_time()/rv3028_set_time(). No
 * software day-advance step is needed: the chip keeps its own calendar
 * current in hardware.
 */
void set_date(Date d);

/**
 * @brief Get the current calendar date.
 *
 * Backed by the RV-3028 hardware RTC (rv3028_get_date()).
 */
Date get_date(void);

/**
 * @brief Returns a 3-letter abbreviation ("SUN".."SAT") for the day of week
 *        of the given date. Same Sakamoto's-algorithm as rv3028.c's
 *        (file-local) day_of_week() — kept separate since that one feeds the
 *        RTC's raw weekday register, this one is purely for display.
 */
const char *get_day_of_week_str(Date d);

uint8_t increment_month(uint8_t month, direction_t dir);
uint8_t increment_day(uint8_t day, uint8_t month, uint16_t year, direction_t dir);
uint16_t increment_year(uint16_t year, direction_t dir);


#endif

