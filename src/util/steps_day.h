#ifndef SRC_UTIL_STEPS_DAY_H_
#define SRC_UTIL_STEPS_DAY_H_

/*
 * steps_day: turns the pedometer's cumulative total into a daily count and an
 * hour-by-hour histogram.
 *
 * The daily-count half moved here verbatim from steps_today() in main.c; the
 * hourly half is new, and is why the move happened. Both need exactly the same
 * thing — a day-change edge detected off the RTC, and a baseline subtracted
 * from a cumulative counter — and keeping them apart would have meant two
 * copies of that logic drifting against each other at midnight.
 *
 * This module is deliberately the ONE place that pairs the pedometer with the
 * clock. imu.c stays RTC-free (a sensor driver has no business depending on
 * the wall clock), and the UI asks here rather than doing the arithmetic in a
 * screen.
 *
 * A histogram rather than a running line, because steps are a RATE that only
 * has meaning over an interval: a cumulative step line is monotonic and its
 * shape says nothing you can read at a glance, while "when did I actually
 * move today" is exactly what a per-hour bar chart shows.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STEPS_DAY_HOURS 24


/*
 * steps_day_update: feed the pedometer's cumulative total, once per sensor
 * tick. Detects day and hour rollovers internally.
 *
 * Returns the count since local midnight — the value the clock face's badge
 * shows — so the caller does not need a second call for it.
 *
 * Behaviour deliberately preserved from the original steps_today():
 *
 *   - No rollover while the clock is unset. rv3028_time_is_set() is false
 *     until the RTC has a real time, and get_date() is not a real date until
 *     then; latching a baseline off it and re-latching once the clock IS set
 *     would show a spurious reset the moment the user synced their watch. The
 *     running total is returned instead, and no hourly buckets are filled.
 *   - No attempt to survive a reboot. The pedometer total is RAM-only and
 *     restarts at 0 every boot, so the baseline restarts with it and a reboot
 *     mid-day yields steps-since-boot. That is the underlying counter's
 *     behaviour, not something this adds; making it durable means persisting
 *     both numbers, which is separate work.
 *
 * Rollover lands within one sensor tick (9 s) of the boundary.
 */
uint32_t steps_day_update(uint32_t cumulative);


/*
 * steps_day_total: the most recent value steps_day_update() returned, without
 * re-reading the sensor. For screens that draw between sensor ticks.
 */
uint32_t steps_day_total(void);


/*
 * steps_day_hours: copies the 24 per-hour step counts into out, index 0 =
 * 00:00-01:00 local. Returns the number written (STEPS_DAY_HOURS, or 0 if the
 * clock has never been set and no day has therefore ever started).
 *
 * Counts are clamped to INT16_MAX so they can be plotted directly; nobody
 * takes 32767 steps in an hour, so the clamp is a type guard rather than a
 * real limit.
 */
size_t steps_day_hours(int16_t *out, size_t max);


/*
 * steps_day_current_hour: local hour 0-23 that is currently accumulating, or
 * -1 if the clock is unset. The graph uses it to mark the in-progress bar,
 * which is otherwise indistinguishable from a genuinely quiet hour.
 */
int steps_day_current_hour(void);


/*
 * steps_day_rev: bumped whenever a bucket changes, so a redraw-on-tick screen
 * can skip repainting an unchanged histogram.
 */
uint32_t steps_day_rev(void);

#endif /* SRC_UTIL_STEPS_DAY_H_ */
