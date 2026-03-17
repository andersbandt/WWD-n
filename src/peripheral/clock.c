//*****************************************************************************
//!
//! @file clock.c
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

/* Standard C99 stuff */
#include <stdint.h>
#include <stdbool.h>


/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>


/* my driver files */
#include <peripheral/clock.h>


LOG_MODULE_REGISTER(clock, LOG_LEVEL_INF);


uint32_t raw_ms = 0;
static int ticks_overflow = 0;
static uint32_t prev_ticks = 0;

static uint32_t tick_offset = 0;

Date current_date = {.day = 1, .month = 1, .year = 2026};

/* Days per month for a non-leap year */
static const uint8_t month_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL FUNCTIONS -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*
 * get_current_time: returns the current time in uint32_t format instead of a struct
 */
// uint32_t get_current_time_uint32() {
//     return (time_offset.hours << 16) | (time_offset.minutes << 8) | time_offset.seconds;
// }



Time add_time(Time t1, Time t2) {
    Time result;

    // Add seconds and handle overflow
    result.seconds = t1.seconds + t2.seconds;
    result.minutes = t1.minutes + t2.minutes + (result.seconds / 60);
    result.seconds %= 60;

    // Add minutes and handle overflow
    result.hours = t1.hours + t2.hours + (result.minutes / 60);
    result.minutes %= 60;

    // Handle 24-hour wraparound
    result.hours %= 24;

    return result;
}


static inline uint8_t wrap_inc(uint8_t value, uint8_t max, direction_t dir) {
    if (dir) {
        return (value >= max) ? 0 : value + 1;
    } else {
        return (value == 0) ? max : value - 1;
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* ---- Date algorithms ---- */

bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t days_in_month(uint8_t month, uint16_t year)
{
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return month_days[month - 1];
}

Date increment_date(Date d)
{
    d.day++;
    if (d.day > days_in_month(d.month, d.year)) {
        d.day = 1;
        d.month++;
        if (d.month > 12) {
            d.month = 1;
            d.year++;
        }
    }
    return d;
}

void set_date(Date d)
{
    current_date = d;
}

Date get_date(void)
{
    return current_date;
}

void clock_advance_date(void)
{
    current_date = increment_date(current_date);
    LOG_INF("Date advanced: %04u-%02u-%02u",
            current_date.year, current_date.month, current_date.day);
}


/* ---- Time ---- */

uint8_t increment_second(uint8_t s, direction_t dir) {
    return wrap_inc(s, 59, dir);
}

uint8_t increment_minute(uint8_t m, direction_t dir) {
    return wrap_inc(m, 59, dir);
}

uint8_t increment_hour(uint8_t h, direction_t dir) {
    return wrap_inc(h, 23, dir);
}



uint32_t get_raw_ticks() {
    uint32_t ticks = sys_clock_tick_get();

    // handle overflow condition
    if (ticks < prev_ticks) {
        ticks_overflow++;
    }
    prev_ticks = ticks;
    
    return ticks;
}


// TODO: really think about when this will get run and when `get_raw_ticks` will get run
uint32_t get_dt_ticks() {
    uint32_t ticks = sys_clock_tick_get();
    return ticks - prev_ticks;
}


uint32_t get_ms(void) {
    raw_ms = get_raw_ticks();
    raw_ms = raw_ms - tick_offset;
    
    // METHOD 3: using double
    /* double slope = 7193; */
    double slope = 9900.6656;
    uint32_t correction = slope*raw_ms / 10000;

    // calculate current actual time in ms
    uint32_t y = raw_ms - correction;
    y = y + ticks_overflow*(UINT32_MAX - slope*UINT32_MAX / 10000);
    
    return y;
}


/*
 * get_sys_time: 
 */
Time get_sys_time() {
    Time sys_time;
    uint32_t cur_ms = k_uptime_get_32();  // Get system uptime in milliseconds
    
    // Calculate hours
    sys_time.hours = cur_ms / (3600 * 1000);
    cur_ms %= (3600 * 1000);
    
    // Calculate minutes (remaining milliseconds after hours are removed)
    sys_time.minutes = cur_ms / (60 * 1000);
    cur_ms %= (60 * 1000);
    
    // Calculate seconds (remaining milliseconds after minutes are removed)
    sys_time.seconds = cur_ms / 1000;
    
    return sys_time;
}


/*
 * get_current_time: returns RTC time
 */
Time get_current_time() {
    return add_time(get_sys_time(), time_offset);
    return get_sys_time();
}

/*
 *
 */
void set_time_offset(Time t) {
    time_offset = t;
    ticks_overflow = 0;
}



/*
 * 
 */
void print_time() {
    Time cur_time = get_current_time();
    
    LOG_DBG("\n%d,%u", ticks_overflow, prev_ticks);
    LOG_DBG("[%d:%d:%d]",
                   cur_time.hours,
                   cur_time.minutes,
                   cur_time.seconds);
}





