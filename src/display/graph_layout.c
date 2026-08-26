/*
 * graph_layout: pure geometry/scaling for plotted graphs. See graph_layout.h
 * for why this is separate from the drawing code.
 */

#include <stdio.h>

#include "graph_layout.h"


struct graph_box graph_plot_rect(struct graph_box outer,
                                 int16_t y_label_w, int16_t x_label_h)
{
    struct graph_box inner = outer;

    inner.left = (int16_t)(outer.left + y_label_w);
    inner.bottom = (int16_t)(outer.bottom - x_label_h);

    /* A caller can ask for margins that do not fit (a tiny box, or a long
     * y label). Collapsing to a degenerate rect and letting the drawing code
     * bail is better than returning an inverted one that would underflow the
     * unsigned pixel math downstream. */
    if (inner.left > inner.right) {
        inner.left = inner.right;
    }
    if (inner.bottom < inner.top) {
        inner.bottom = inner.top;
    }

    return inner;
}


int16_t graph_value_to_y(int32_t value, int32_t y_min, int32_t y_max,
                         int16_t top, int16_t bottom)
{
    if (y_max <= y_min || bottom <= top) {
        return bottom;
    }

    if (value < y_min) {
        value = y_min;
    }
    if (value > y_max) {
        value = y_max;
    }

    int32_t height = (int32_t)bottom - (int32_t)top;
    int32_t span = y_max - y_min;

    /* Rounded, not truncated: at 1 mV per 3 px a truncating divide biases
     * every point downward by up to a pixel, which shows up as a plot that
     * drifts below a flat line it should sit on. */
    int32_t offset = ((value - y_min) * height + span / 2) / span;

    return (int16_t)(bottom - offset);
}


int16_t graph_col_to_x(uint16_t col, uint16_t cols, int16_t left, int16_t right)
{
    if (cols <= 1) {
        return left;
    }
    if (col >= cols) {
        col = (uint16_t)(cols - 1);
    }

    int32_t width = (int32_t)right - (int32_t)left;

    return (int16_t)(left + ((int32_t)col * width + (cols - 1) / 2) / (cols - 1));
}


void graph_column_span(size_t n, uint16_t cols, uint16_t col,
                       size_t *from, size_t *to)
{
    if (from == NULL || to == NULL) {
        return;
    }

    if (n == 0 || cols == 0) {
        *from = 0;
        *to = 0;
        return;
    }

    if (col >= cols) {
        col = (uint16_t)(cols - 1);
    }

    *from = ((size_t)col * n) / cols;
    *to = ((size_t)(col + 1) * n) / cols;

    /* With more columns than samples the division above hands some columns an
     * empty range. Give them the one sample they straddle instead: a gap in
     * the middle of a sparse plot reads as missing data, which is a different
     * and much more alarming claim than "we only have 9 points". */
    if (*to <= *from) {
        *from = ((size_t)col * n) / cols;
        if (*from >= n) {
            *from = n - 1;
        }
        *to = *from + 1;
    }
}


void graph_minmax(const int16_t *data, size_t n, int16_t *out_min, int16_t *out_max)
{
    if (data == NULL || n == 0) {
        return;
    }

    int16_t lo = data[0];
    int16_t hi = data[0];

    for (size_t i = 1; i < n; i++) {
        if (data[i] < lo) {
            lo = data[i];
        }
        if (data[i] > hi) {
            hi = data[i];
        }
    }

    if (out_min != NULL) {
        *out_min = lo;
    }
    if (out_max != NULL) {
        *out_max = hi;
    }
}


void graph_pad_range(int16_t *y_min, int16_t *y_max)
{
    if (y_min == NULL || y_max == NULL) {
        return;
    }

    if (*y_max > *y_min) {
        return;
    }

    if (*y_max < *y_min) {          /* inverted — normalise before padding */
        int16_t tmp = *y_min;
        *y_min = *y_max;
        *y_max = tmp;
        if (*y_max > *y_min) {
            return;
        }
    }

    /* Saturate rather than wrap. A series pinned at INT16_MAX is pathological
     * but it is also exactly what an uninitialised or overflowed sensor read
     * looks like, and wrapping here would turn that into a graph drawn upside
     * down rather than a flat line at the top. */
    if (*y_min > INT16_MIN) {
        (*y_min)--;
    }
    if (*y_max < INT16_MAX) {
        (*y_max)++;
    }
}


char *graph_format_ago(uint32_t seconds_ago, char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return buf;
    }

    if (seconds_ago < 5) {
        snprintf(buf, len, "now");
    } else if (seconds_ago < 60) {
        snprintf(buf, len, "-%us", (unsigned)seconds_ago);
    } else if (seconds_ago < 3600) {
        snprintf(buf, len, "-%um", (unsigned)(seconds_ago / 60));
    } else {
        snprintf(buf, len, "-%uh", (unsigned)(seconds_ago / 3600));
    }

    return buf;
}


char *graph_format_clock(uint8_t hours, uint8_t minutes, uint32_t seconds_ago,
                         char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return buf;
    }

    int32_t total = (int32_t)hours * 60 + (int32_t)minutes - (int32_t)(seconds_ago / 60);

    /* Wrap backwards over as many midnights as the span covers. A day-long
     * battery plot's left edge is yesterday, and the label has no date field,
     * so this is showing a time of day and nothing more — which is what a
     * reader glancing at a 92 px plot wants from it. */
    while (total < 0) {
        total += 24 * 60;
    }
    total %= 24 * 60;

    snprintf(buf, len, "%02u:%02u", (unsigned)(total / 60), (unsigned)(total % 60));

    return buf;
}
