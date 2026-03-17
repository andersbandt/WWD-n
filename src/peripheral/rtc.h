//*****************************************************************************
//!
//! @file rtc.h
//! @author Anders Bandt
//! @brief Hardware RTC driver for nRF52832/nRF52840 using Zephyr counter API
//! @version 0.1
//! @date March 2026
//!
//! Uses RTC2 peripheral via Zephyr's counter driver API.
//! RTC0 is reserved for the BLE SoftDevice; RTC1 is used by the Zephyr kernel
//! timer — RTC2 is the safe choice for application use.
//!
//! The driver sets up a 1-second repeating alarm and maintains a
//! seconds-since-midnight counter. Time is exposed via the existing Time struct
//! from clock.h.
//!
//! Accuracy note: prj.conf currently selects the RC oscillator
//! (CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y). The RC oscillator drifts more
//! than a crystal. For better accuracy switch to:
//!     CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y
//!
//*****************************************************************************

#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <peripheral/clock.h>   /* Time struct */


/**
 * @brief Initialize the RTC2 peripheral and start the 1-second tick.
 *
 * Must be called before rtc_set_time() or rtc_get_time(). The RTC continues
 * running through Zephyr sleep states as long as the LFCLK is kept active.
 *
 * @return 0 on success, negative errno on failure.
 */
int rtc_init(void);


/**
 * @brief Set the current wall-clock time.
 *
 * Converts the Time struct to an internal seconds-since-midnight counter.
 * The next 1-second tick will advance from this value.
 *
 * @param t  Time to set (hours 0–23, minutes 0–59, seconds 0–59).
 */
void rtc_set_time(Time t);


/**
 * @brief Get the current wall-clock time.
 *
 * @return Current time as a Time struct.
 */
Time rtc_get_time(void);


#endif /* RTC_H */
