/*
 * steps_day: daily + hourly step accounting. See steps_day.h for the contract
 * and for what this deliberately does not do.
 */

#include <zephyr/kernel.h>

#include <peripheral/clock.h>
#include <peripheral/rv3028.h>
#include <util/steps_day.h>


static uint32_t day_total;                    /* steps since local midnight */
static uint32_t day_base;                     /* cumulative total as of the day's start */
static int32_t  base_day = -1;                /* day-of-month the baseline was taken on */

static uint32_t hour_base;                    /* cumulative total as of the hour's start */
static int32_t  base_hour = -1;               /* hour the bucket below is filling */
static uint16_t buckets[STEPS_DAY_HOURS];
static bool     day_started;                  /* a real date has been seen at least once */
static uint32_t rev;

/* The sensor thread writes, whichever thread draws reads. Cheap to take: the
 * whole critical section is a handful of integer ops. */
K_MUTEX_DEFINE(steps_day_lock);


static uint16_t clamp_bucket(uint32_t steps)
{
    return (steps > INT16_MAX) ? (uint16_t)INT16_MAX : (uint16_t)steps;
}


uint32_t steps_day_update(uint32_t cumulative)
{
    if (!rv3028_time_is_set()) {
        /* No trustworthy date yet — show the running total and take no
         * baseline, so the first real date does not read as a reset. */
        k_mutex_lock(&steps_day_lock, K_FOREVER);
        day_total = cumulative;
        k_mutex_unlock(&steps_day_lock);
        return cumulative;
    }

    Date today = get_date();
    Time now = get_current_time();
    int32_t hour = (int32_t)now.hours;

    k_mutex_lock(&steps_day_lock, K_FOREVER);

    /* A reboot leaves these statics behind in a way nothing else can, so the
     * baseline can exceed the total. Rebase rather than underflow a uint32. */
    if (cumulative < day_base) {
        day_base = cumulative;
    }
    if (cumulative < hour_base) {
        hour_base = cumulative;
    }

    if ((int32_t)today.day != base_day) {
        day_base = cumulative;
        base_day = (int32_t)today.day;
        day_started = true;

        /* A new day starts empty. Zeroing here rather than incrementally as
         * each hour comes round means yesterday's 23:00 bar disappears at
         * midnight instead of lingering for a day in a bucket nothing has
         * revisited yet. */
        for (size_t i = 0; i < STEPS_DAY_HOURS; i++) {
            buckets[i] = 0;
        }

        hour_base = cumulative;
        base_hour = hour;
        rev++;
    } else if (hour != base_hour) {
        /* Close the hour that just ended at its final value before moving on.
         * Hours the device skipped (nothing pushed for over an hour) keep
         * whatever they had, which is 0 — correct, since no steps were
         * attributed to them. */
        if (base_hour >= 0 && base_hour < STEPS_DAY_HOURS) {
            buckets[base_hour] = clamp_bucket(cumulative - hour_base);
        }
        hour_base = cumulative;
        base_hour = hour;
        rev++;
    }

    if (base_hour >= 0 && base_hour < STEPS_DAY_HOURS) {
        uint16_t updated = clamp_bucket(cumulative - hour_base);
        if (updated != buckets[base_hour]) {
            buckets[base_hour] = updated;
            rev++;
        }
    }

    day_total = cumulative - day_base;
    uint32_t out = day_total;

    k_mutex_unlock(&steps_day_lock);

    return out;
}


uint32_t steps_day_total(void)
{
    k_mutex_lock(&steps_day_lock, K_FOREVER);
    uint32_t out = day_total;
    k_mutex_unlock(&steps_day_lock);
    return out;
}


size_t steps_day_hours(int16_t *out, size_t max)
{
    if (out == NULL || max == 0) {
        return 0;
    }

    k_mutex_lock(&steps_day_lock, K_FOREVER);

    if (!day_started) {
        k_mutex_unlock(&steps_day_lock);
        return 0;
    }

    size_t n = (max < STEPS_DAY_HOURS) ? max : STEPS_DAY_HOURS;
    for (size_t i = 0; i < n; i++) {
        out[i] = (int16_t)buckets[i];
    }

    k_mutex_unlock(&steps_day_lock);
    return n;
}


int steps_day_current_hour(void)
{
    k_mutex_lock(&steps_day_lock, K_FOREVER);
    int out = day_started ? (int)base_hour : -1;
    k_mutex_unlock(&steps_day_lock);
    return out;
}


uint32_t steps_day_rev(void)
{
    k_mutex_lock(&steps_day_lock, K_FOREVER);
    uint32_t out = rev;
    k_mutex_unlock(&steps_day_lock);
    return out;
}
