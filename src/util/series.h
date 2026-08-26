#ifndef SRC_UTIL_SERIES_H_
#define SRC_UTIL_SERIES_H_

/*
 * series: a small fixed-capacity time-series ring for the graph screens.
 *
 * Generalised out of the temperature history that used to live (and still
 * lives, for the paired IMU/SoC case) in imu.c. Every graph on this device
 * wants the same four things — append a sample, read the last N oldest-first,
 * know whether anything changed since the last draw, know how far back the
 * oldest sample reaches — and the temperature ring had grown all four as
 * one-off statics. A second and third graph would have copied them.
 *
 * Two deliberate properties:
 *
 * DECIMATION WITH AVERAGING. A caller pushes at whatever rate it already
 * samples (the sensor thread's 9 s tick) and the ring stores one value per
 * `decimate` pushes, as the MEAN of that window. Plain sub-sampling would be
 * cheaper and wrong: battery voltage is noisy per-reading, and keeping every
 * 33rd raw sample plots the noise rather than the trend. The mean also makes
 * the stored cadence honest — each stored point really does represent its
 * whole interval.
 *
 * TIME BY CADENCE, NOT TIMESTAMPS. Samples carry no per-sample timestamp; the
 * ring knows its nominal seconds-per-stored-sample and the uptime of the most
 * recent store. Two bytes per sample instead of six, and on this device the
 * cadence is genuinely fixed — the producer is a periodic thread that runs
 * whether or not the display is awake. series_age_of_oldest() is therefore
 * derived, and it is derived from the real clock rather than from count *
 * interval so that a stall (a long ERASE, a paused background thread) shows
 * up as a stretched axis instead of being silently ignored.
 *
 * Storage is caller-supplied so the ring itself allocates nothing and each
 * screen's RAM cost is visible at its definition site.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

struct series {
    int16_t  *buf;
    uint16_t  cap;          /* buf capacity in samples */
    uint16_t  count;        /* valid samples, saturates at cap */
    uint16_t  head;         /* next write index */
    uint32_t  rev;          /* bumped on every stored sample */

    uint16_t  interval_s;   /* nominal seconds per STORED sample */
    uint16_t  decimate;     /* pushes per stored sample (>= 1) */
    uint16_t  pending;      /* pushes accumulated toward the next store */
    int32_t   accum;        /* sum of the pending window, for the mean */

    int64_t   last_store_ms;/* k_uptime_get() of the newest stored sample */

    struct k_mutex lock;
};


/*
 * series_init: binds a ring to caller-owned storage.
 *
 * push_interval_s is the cadence the PRODUCER calls series_push() at; the
 * stored cadence is push_interval_s * decimate, which is what the time axis
 * is drawn from. Passing the producer's rate rather than the stored rate
 * keeps the two numbers from drifting apart when a decimation factor changes.
 */
void series_init(struct series *s, int16_t *storage, uint16_t capacity,
                 uint16_t push_interval_s, uint16_t decimate);


/*
 * series_push: feeds one raw sample in. Stores a value (the mean of the
 * decimation window) only every `decimate` calls; the rest just accumulate.
 */
void series_push(struct series *s, int16_t value);


/*
 * series_get: copies up to max_count of the most recent stored samples into
 * out, OLDEST FIRST so it can be handed straight to drawGraphEx() left to
 * right. Returns how many were copied.
 */
size_t series_get(struct series *s, int16_t *out, size_t max_count);


/*
 * series_get_rev: counter incremented on every stored sample. Lets a screen
 * redrawn on a timer skip the redraw when nothing landed since last time —
 * a line graph cannot be partially repainted the way a digit field can, so
 * not redrawing at all is the only cheap option.
 */
uint32_t series_get_rev(struct series *s);


/*
 * series_span_seconds: how far back `n` stored samples reach, in seconds.
 *
 * Measured against the clock where possible (the age of the oldest sample
 * relative to the newest, plus the newest's own age) rather than assumed from
 * n * interval, so a gap in production stretches the axis instead of
 * compressing real time into a shorter-looking window.
 */
uint32_t series_span_seconds(struct series *s, size_t n);


/*
 * series_reset: drops every sample and the pending accumulator. For a screen
 * whose underlying meaning has changed (a day rollover, a chip erase) rather
 * than for routine use.
 */
void series_reset(struct series *s);

#endif /* SRC_UTIL_SERIES_H_ */
