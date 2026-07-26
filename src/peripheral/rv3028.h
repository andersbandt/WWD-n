#ifndef RV3028_H
#define RV3028_H

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

void rv3028_print_time(void);

#endif /* RV3028_H */
