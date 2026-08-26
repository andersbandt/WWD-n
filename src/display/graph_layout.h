#ifndef SRC_DISPLAY_GRAPH_LAYOUT_H_
#define SRC_DISPLAY_GRAPH_LAYOUT_H_

/*
 * graph_layout: the arithmetic behind a plotted graph, with no drawing in it.
 *
 * Split out from display.c on purpose. Everything here is pure integer math
 * over plain types — no Zephyr, no SPI, no globals — so it compiles for the
 * host and can be tested exhaustively on a laptop with no board attached
 * (test/band/graph_test.c). That matters more than usual for this code: a
 * scaling bug does not crash, it just draws a plausible-looking wrong picture,
 * which is exactly the class of bug you cannot catch by looking at a 128x160
 * panel across the room.
 *
 * The drawing side (drawGraphEx() in display.c) is then thin enough to review
 * by eye: it walks columns, asks this file where each one goes, and draws.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Inclusive pixel rect, matching the convention drawRect()/filledRect() in
 * gfx.c already use. */
struct graph_box {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
};


/*
 * graph_plot_rect: carves the inner plotting area out of the outer box by
 * reserving a left margin for y-axis labels and a bottom margin for x-axis
 * (time) labels.
 *
 * The primitive owns its whole outer rect — margins included — because the
 * alternative (caller reserves margins, primitive draws the box) is what the
 * temperature screen used to do, and it left a strip of the previous screen
 * showing beside the plot. One owner, one clear.
 */
struct graph_box graph_plot_rect(struct graph_box outer,
                                 int16_t y_label_w, int16_t x_label_h);


/*
 * graph_value_to_y: maps a sample value onto a pixel row inside the plot,
 * clamped to the box. y_max sits at `top`, y_min at `bottom` (screen y grows
 * downward). Requires y_max > y_min; returns `bottom` if that is violated
 * rather than dividing by zero.
 *
 * Takes int32_t so a caller can pass millivolts or step counts without
 * pre-scaling, even though the sample rings themselves store int16_t.
 */
int16_t graph_value_to_y(int32_t value, int32_t y_min, int32_t y_max,
                         int16_t top, int16_t bottom);


/*
 * graph_col_to_x: pixel x of column `col` of `cols`, spread across
 * [left, right] inclusive. col 0 lands on `left`, col cols-1 on `right`.
 */
int16_t graph_col_to_x(uint16_t col, uint16_t cols, int16_t left, int16_t right);


/*
 * graph_column_span: which samples fall in column `col`, as the half-open
 * range [*from, *to).
 *
 * This is what makes a 288-sample day fit in a 90 px plot without lying. The
 * naive fix — plot every Nth sample — drops the sample that mattered: a
 * battery sag or a step burst that lasted one bucket vanishes entirely.
 * Aggregating a column's whole span (drawGraphEx draws min..max as a vertical
 * run) keeps every excursion visible at the cost of the exact shape between
 * them, which is the right trade at ~3 samples per pixel.
 *
 * Always yields a non-empty range for every column when n >= cols, and an
 * empty one is never returned — callers may assume *to > *from.
 */
void graph_column_span(size_t n, uint16_t cols, uint16_t col,
                       size_t *from, size_t *to);


/*
 * graph_minmax: extremes of a sample array. Leaves both outputs untouched if
 * n == 0, so a caller can pre-seed them.
 */
void graph_minmax(const int16_t *data, size_t n, int16_t *out_min, int16_t *out_max);


/*
 * graph_pad_range: widens a degenerate or inverted range until max > min, so
 * a flat series (every sample identical — very common for a battery sitting
 * on a charger) still plots as a line through the middle instead of being
 * rejected by the y_max > y_min guard.
 */
void graph_pad_range(int16_t *y_min, int16_t *y_max);


/*
 * graph_format_ago: relative time label for a point `seconds_ago` behind now,
 * into a caller-supplied buffer — "now", "-45s", "-9m", "-2h", "-24h".
 *
 * Deliberately coarse and short: these sit under a plot that is at most ~92 px
 * wide, where a label is 6 px per character, so three of them have to share
 * roughly 15 characters between them.
 *
 * Returns buf.
 */
char *graph_format_ago(uint32_t seconds_ago, char *buf, size_t len);


/*
 * graph_format_clock: absolute wall-clock label ("14:05") for a point
 * `seconds_ago` behind the supplied current time, wrapping backwards over
 * midnight.
 *
 * Preferred over graph_format_ago() whenever the RTC is set — "14:05" answers
 * "when was that dip" and "-2h" only answers it if you also know what time it
 * is now. Callers fall back to the relative form when rv3028_time_is_set() is
 * false, since an unset clock makes an absolute label a confident lie.
 */
char *graph_format_clock(uint8_t hours, uint8_t minutes, uint32_t seconds_ago,
                         char *buf, size_t len);

#endif /* SRC_DISPLAY_GRAPH_LAYOUT_H_ */
