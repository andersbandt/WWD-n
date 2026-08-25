#ifndef RV3028_H
#define RV3028_H

#include <stdbool.h>
#include <peripheral/clock.h>   /* Time struct */

/*
 * RV-3028-C7 hardware RTC driver (Zephyr RTC API).
 * Alternative to the software counter approach in rtc.c.
 *
 * To switch get_current_time() to this driver:
 *   1. Uncomment CONFIG_RTC / CONFIG_RTC_RV3028 in prj.conf
 *   2. Change get_current_time() in clock.c to call rv3028_get_time()
 *      instead of rtc_get_time()
 */

void rv3028_init(void);
Time rv3028_get_time(void);
void rv3028_set_time(Time t);
Date rv3028_get_date(void);
void rv3028_set_date(Date d);

/* Raw 32-bit UNIX seconds-since-epoch counter (regs 0x1B-0x1E), independent
 * of the BCD seconds..year block above. Not synced automatically with it —
 * for cheap timestamp/duration math, not display. */
uint32_t rv3028_get_unix_time(void);
void rv3028_set_unix_time(uint32_t t);

/**
 * @brief True if the RV-3028 is bound AND holds a time that has actually been
 *        set, rather than power-on defaults.
 *
 * This is what decides RECORD_TIME_ANCHOR's time_valid flag, so decoders use
 * it to choose between real wall-clock timestamps and relative-only time.
 *
 * The test is a plausible year (>= RV3028_MIN_VALID_YEAR). The part has no
 * "has been set" bit exposed through Zephyr's RTC API, and a cold RV-3028
 * comes up at its default epoch, so the year is the available signal.
 *
 * NOTE what this does and does not claim. It says the fields came from a real,
 * running, set RTC — NOT that the RTC is set CORRECTLY. A clock set to the
 * wrong hour still reports valid, and the decoder will then produce
 * confidently wrong wall times. Nothing in firmware can detect that; keeping
 * the device's clock right is a user action.
 */
bool rv3028_time_is_set(void);

void rv3028_print_time(void);

#endif /* RV3028_H */
