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

/* Standard C99 stuff */
#include <stdint.h>

/* Driver Header files  */
#include <ti/drivers/dpl/ClockP.h>
#include <ti/display/Display.h>
#include "config/ti_drivers_config.h"

/* My header files  */
#include <src/clock.h>

uint32_t raw_ms = 0;
static int ticks_overflow = 0;
static uint32_t prev_ticks = 0;


static uint32_t tick_offset = 0;

#define SLOPE_VALUE 6652


void init_time_offset() {
    time_offset.hours = 10;
    time_offset.minutes = 25;
    time_offset.seconds = 37;
}


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


// TODO: possible move these to like arithmetic helper file? Same with above function?
uint8_t increment_second(uint8_t second) {
    second += 1;
    if (second > 59) {
        second = 0;
    }
    return second;
}


uint8_t increment_hour(uint8_t hour) {
    hour += 1;
    if (hour > 23) {
        hour = 0;
    }
    return hour;
}


uint8_t increment_minute(uint8_t minute) {
        minute += 1;
        if (minute > 60) {
            minute = 0;
        }
        return minute;
}



// the clock period is 10us last I checked
uint32_t get_clock_period() {
    return ClockP_getSystemTickPeriod();
}

/*
 *
 */
uint32_t get_slope() {
    return SLOPE_VALUE;
}

/*
 *
 */
uint32_t get_raw_ms() {
    uint32_t ticks = ClockP_getSystemTicks();

    // handle overflow condition
    if (ticks < prev_ticks) {
        ticks_overflow++;
    }
    prev_ticks = ticks;
    
    return ticks;
}


/*
 * get_ms: 
 */
uint32_t get_ms(void) {
    raw_ms = get_raw_ms();
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
    uint32_t cur_ms = get_ms();

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
}

/*
 *
 */
void set_time_offset() {
    tick_offset = ClockP_getSystemTicks();
    ticks_overflow = 0;
}

/*
 * get_current_time: returns the current time
 */
uint32_t get_current_time_uint32() {
    return (time_offset.hours << 16) | (time_offset.minutes << 8) | time_offset.seconds;
}


/*
 *
 */
void print_time(Display_Handle display) {
    uint32_t cur_ms = get_ms();
    Time cur_time = get_current_time();
    
    LOG_INF(display, 0, 0, "\n%d,%u", ticks_overflow, prev_ticks);
    LOG_INF(display, 0, 0, "%u,%u,[%d:%d:%d]", raw_ms, cur_ms,
                   cur_time.hours,
                   cur_time.minutes,
                   cur_time.seconds);
}





