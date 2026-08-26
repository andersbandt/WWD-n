/*
 * series: fixed-capacity time-series ring. See series.h for the design notes
 * (decimation-with-averaging, and why samples carry no timestamps).
 */

#include <util/series.h>


void series_init(struct series *s, int16_t *storage, uint16_t capacity,
                 uint16_t push_interval_s, uint16_t decimate)
{
    if (s == NULL || storage == NULL || capacity == 0) {
        return;
    }

    s->buf = storage;
    s->cap = capacity;
    s->count = 0;
    s->head = 0;
    s->rev = 0;
    s->decimate = (decimate == 0) ? 1 : decimate;
    s->interval_s = (uint16_t)(push_interval_s * s->decimate);
    s->pending = 0;
    s->accum = 0;
    s->last_store_ms = 0;

    k_mutex_init(&s->lock);
}


void series_push(struct series *s, int16_t value)
{
    if (s == NULL || s->buf == NULL) {
        return;
    }

    k_mutex_lock(&s->lock, K_FOREVER);

    s->accum += value;
    s->pending++;

    if (s->pending >= s->decimate) {
        /* Mean of the window, rounded away from zero so a slowly falling
         * battery does not pick up a systematic downward bias from repeated
         * truncation. */
        int32_t n = s->pending;
        int32_t mean = (s->accum >= 0) ? (s->accum + n / 2) / n
                                       : (s->accum - n / 2) / n;

        s->buf[s->head] = (int16_t)mean;
        s->head = (uint16_t)((s->head + 1) % s->cap);
        if (s->count < s->cap) {
            s->count++;
        }
        s->rev++;
        s->last_store_ms = k_uptime_get();

        s->pending = 0;
        s->accum = 0;
    }

    k_mutex_unlock(&s->lock);
}


size_t series_get(struct series *s, int16_t *out, size_t max_count)
{
    if (s == NULL || s->buf == NULL || out == NULL || max_count == 0) {
        return 0;
    }

    k_mutex_lock(&s->lock, K_FOREVER);

    size_t n = (s->count < max_count) ? s->count : max_count;
    size_t oldest = (size_t)(s->head + s->cap - s->count) % s->cap;
    size_t start = (oldest + (s->count - n)) % s->cap;

    for (size_t i = 0; i < n; i++) {
        out[i] = s->buf[(start + i) % s->cap];
    }

    k_mutex_unlock(&s->lock);
    return n;
}


uint32_t series_get_rev(struct series *s)
{
    if (s == NULL) {
        return 0;
    }

    k_mutex_lock(&s->lock, K_FOREVER);
    uint32_t rev = s->rev;
    k_mutex_unlock(&s->lock);

    return rev;
}


uint32_t series_span_seconds(struct series *s, size_t n)
{
    if (s == NULL || n == 0) {
        return 0;
    }

    k_mutex_lock(&s->lock, K_FOREVER);

    if (n > s->count) {
        n = s->count;
    }
    if (n == 0) {
        /* Clamping an over-large request against an empty ring: (n - 1) on an
         * unsigned would wrap to a span of decades. */
        k_mutex_unlock(&s->lock);
        return 0;
    }

    /* Interval between the oldest and newest of n samples is (n-1) gaps, plus
     * however long ago the newest one was stored — the reader is looking at
     * the plot NOW, not at the moment the last sample landed, and on a 24 h
     * axis that difference is invisible while on a 9 minute one it is not. */
    uint32_t span = (uint32_t)(n - 1) * s->interval_s;

    if (s->last_store_ms > 0) {
        int64_t age_ms = k_uptime_get() - s->last_store_ms;
        if (age_ms > 0) {
            span += (uint32_t)(age_ms / 1000);
        }
    }

    k_mutex_unlock(&s->lock);
    return span;
}


void series_reset(struct series *s)
{
    if (s == NULL) {
        return;
    }

    k_mutex_lock(&s->lock, K_FOREVER);

    s->count = 0;
    s->head = 0;
    s->pending = 0;
    s->accum = 0;
    s->last_store_ms = 0;
    s->rev++;   /* still a change: a screen watching rev must repaint the empty state */

    k_mutex_unlock(&s->lock);
}
