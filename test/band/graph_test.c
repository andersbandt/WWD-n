/*
 * graph_test: host-native tests for src/display/graph_layout.c.
 *
 * Build/run:  make -C test/band run-graph
 *
 * Why this exists at all: a graph that is scaled wrong does not crash, log, or
 * fail to draw. It draws a plausible picture of the wrong thing, on a 128x160
 * panel, usually while you are holding it at arm's length. The only way to
 * catch that class of bug is to check the arithmetic against numbers you
 * worked out by hand — which is what this does, with no board attached.
 *
 * Everything under test is pure integer math over plain types, so this
 * compiles the real graph_layout.c directly with nothing stubbed.
 */

#include <stdio.h>
#include <string.h>

#include "graph_layout.h"

static int failures;
static int checks;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL  %s\n", what);
    }
}

static void eq_i(int got, int want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL  %s: got %d, want %d\n", what, got, want);
    }
}

static void eq_s(const char *got, const char *want, const char *what)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL  %s: got \"%s\", want \"%s\"\n", what, got, want);
    }
}


static void test_plot_rect(void)
{
    printf("plot_rect\n");

    struct graph_box outer = { 4, 4, 124, 156 };
    struct graph_box inner = graph_plot_rect(outer, 32, 14);

    eq_i(inner.left, 36, "left margin reserved");
    eq_i(inner.top, 4, "top untouched");
    eq_i(inner.right, 124, "right untouched");
    eq_i(inner.bottom, 142, "bottom margin reserved");

    /* No labels asked for -> the plot gets the whole box. This is the case
     * that keeps an unlabelled graph from shrinking for no reason. */
    struct graph_box full = graph_plot_rect(outer, 0, 0);
    ok(full.left == outer.left && full.bottom == outer.bottom, "zero margins are a no-op");

    /* Margins bigger than the box must collapse, not invert: an inverted rect
     * would underflow the unsigned pixel math in gfx.c and paint the panel. */
    struct graph_box tiny = { 0, 0, 10, 10 };
    struct graph_box squashed = graph_plot_rect(tiny, 40, 40);
    ok(squashed.left <= squashed.right, "over-wide y margin collapses, not inverts");
    ok(squashed.top <= squashed.bottom, "over-tall x margin collapses, not inverts");
}


static void test_value_to_y(void)
{
    printf("value_to_y\n");

    /* Plot rows 10..110: y_max at the top, y_min at the bottom. */
    eq_i(graph_value_to_y(0, 0, 100, 10, 110), 110, "y_min lands on bottom");
    eq_i(graph_value_to_y(100, 0, 100, 10, 110), 10, "y_max lands on top");
    eq_i(graph_value_to_y(50, 0, 100, 10, 110), 60, "midpoint lands mid-box");

    /* Clamping, not wrapping — one bad sample must not fold the trace. */
    eq_i(graph_value_to_y(-500, 0, 100, 10, 110), 110, "below range clamps to bottom");
    eq_i(graph_value_to_y(9000, 0, 100, 10, 110), 10, "above range clamps to top");

    /* Rounding: 1/3 of a 100 px box is 33.3, and truncation would give 77
     * (bottom-33) for a point that belongs at 76.7 -> 77. Check a case where
     * rounding and truncation actually differ: 2/3 -> 66.7 -> 67. */
    eq_i(graph_value_to_y(67, 0, 100, 10, 110), 110 - 67, "rounds to nearest row");

    /* Degenerate inputs return the baseline rather than dividing by zero. */
    eq_i(graph_value_to_y(5, 10, 10, 10, 110), 110, "empty range is safe");
    eq_i(graph_value_to_y(5, 0, 100, 50, 50), 50, "zero-height box is safe");

    /* int32 inputs: millivolts pass through unscaled. */
    eq_i(graph_value_to_y(3900, 3800, 4200, 0, 100), 75, "millivolt range scales");
}


static void test_col_to_x(void)
{
    printf("col_to_x\n");

    eq_i(graph_col_to_x(0, 10, 20, 110), 20, "first column on the left edge");
    eq_i(graph_col_to_x(9, 10, 20, 110), 110, "last column on the right edge");
    eq_i(graph_col_to_x(5, 10, 20, 110), 70, "middle column spaced evenly");

    eq_i(graph_col_to_x(0, 1, 20, 110), 20, "single column sits at the left");
    eq_i(graph_col_to_x(99, 10, 20, 110), 110, "out-of-range column clamps");

    /* Monotonic and in-bounds for every column of a full-width plot — the
     * property that actually matters, since a non-monotonic x would draw the
     * trace backwards over itself. */
    int prev = -1;
    int monotonic = 1, in_bounds = 1;
    for (unsigned c = 0; c < 90; c++) {
        int x = graph_col_to_x((uint16_t)c, 90, 36, 124);
        if (x < prev) { monotonic = 0; }
        if (x < 36 || x > 124) { in_bounds = 0; }
        prev = x;
    }
    ok(monotonic, "x is monotonic across all columns");
    ok(in_bounds, "x stays inside the plot for all columns");
}


static void test_column_span(void)
{
    printf("column_span\n");

    /* The case this exists for: 288 samples of battery history into an 88 px
     * plot. Every sample must be covered exactly once, or the aggregation is
     * dropping data while looking like it isn't. */
    size_t covered = 0;
    size_t expect_from = 0;
    int contiguous = 1, nonempty = 1;

    for (unsigned c = 0; c < 88; c++) {
        size_t from, to;
        graph_column_span(288, 88, c, &from, &to);
        if (from != expect_from) { contiguous = 0; }
        if (to <= from) { nonempty = 0; }
        covered += to - from;
        expect_from = to;
    }
    eq_i((int)covered, 288, "every sample covered exactly once");
    ok(contiguous, "columns tile the range with no gaps or overlaps");
    ok(nonempty, "no column is empty");
    eq_i((int)expect_from, 288, "last column ends at the last sample");

    /* Sparser than the plot: more columns than samples. Every column must
     * still name a real sample — an empty column would be drawn as a gap,
     * which reads as missing data rather than as a coarse series. */
    int all_valid = 1;
    for (unsigned c = 0; c < 40; c++) {
        size_t from, to;
        graph_column_span(9, 40, c, &from, &to);
        if (to <= from || from >= 9 || to > 9) { all_valid = 0; }
    }
    ok(all_valid, "sparse series gives every column a real sample");

    /* Degenerate inputs. */
    size_t from = 99, to = 99;
    graph_column_span(0, 10, 0, &from, &to);
    ok(from == 0 && to == 0, "empty series yields an empty span");
    graph_column_span(10, 0, 0, &from, &to);
    ok(from == 0 && to == 0, "zero columns yields an empty span");
}


static void test_minmax_and_range(void)
{
    printf("minmax / pad_range\n");

    const int16_t data[] = { 5, -3, 42, 7, 42, -100 };
    int16_t lo = 0, hi = 0;
    graph_minmax(data, 6, &lo, &hi);
    eq_i(lo, -100, "min found");
    eq_i(hi, 42, "max found");

    /* n == 0 leaves the outputs alone so a caller can pre-seed them. */
    lo = 111; hi = 222;
    graph_minmax(data, 0, &lo, &hi);
    ok(lo == 111 && hi == 222, "empty input leaves outputs untouched");

    /* A flat series is the common case for a battery on a charger, and it
     * must still plot rather than being rejected by the y_max > y_min guard. */
    int16_t a = 4100, b = 4100;
    graph_pad_range(&a, &b);
    ok(b > a, "flat range is widened");

    /* Inverted input normalises rather than staying inverted (which would
     * draw the graph upside down). */
    int16_t c = 90, d = 10;
    graph_pad_range(&c, &d);
    ok(d > c, "inverted range is normalised and widened");

    /* Saturation, not wraparound, at the type's limits. */
    int16_t e = INT16_MAX, f = INT16_MAX;
    graph_pad_range(&e, &f);
    ok(f >= e, "range at INT16_MAX does not wrap");
    int16_t g = INT16_MIN, h = INT16_MIN;
    graph_pad_range(&g, &h);
    ok(h >= g, "range at INT16_MIN does not wrap");
}


static void test_labels(void)
{
    printf("axis labels\n");

    char buf[8];

    eq_s(graph_format_ago(0, buf, sizeof(buf)), "now", "0 s is now");
    eq_s(graph_format_ago(4, buf, sizeof(buf)), "now", "under 5 s is still now");
    eq_s(graph_format_ago(45, buf, sizeof(buf)), "-45s", "seconds");
    eq_s(graph_format_ago(540, buf, sizeof(buf)), "-9m", "the temp graph's 9 min span");
    eq_s(graph_format_ago(3600, buf, sizeof(buf)), "-1h", "an hour");
    eq_s(graph_format_ago(86400, buf, sizeof(buf)), "-24h", "the battery graph's day");

    eq_s(graph_format_clock(14, 30, 0, buf, sizeof(buf)), "14:30", "now is now");
    eq_s(graph_format_clock(14, 30, 1800, buf, sizeof(buf)), "14:00", "half an hour back");
    eq_s(graph_format_clock(14, 30, 86400, buf, sizeof(buf)), "14:30", "a full day back wraps to itself");

    /* Backwards over midnight — the left edge of a day-long plot is
     * yesterday, and this is the case that would print a negative hour if the
     * wrap were missing. */
    eq_s(graph_format_clock(0, 15, 3600, buf, sizeof(buf)), "23:15", "wraps back past midnight");
    eq_s(graph_format_clock(1, 0, 7200, buf, sizeof(buf)), "23:00", "wraps back two hours");

    /* Every label must fit the buffer the screens pass (8 bytes) and stay
     * short enough that three of them fit a ~90 px plot at 6 px/char. */
    int all_short = 1;
    for (uint32_t s = 0; s < 200000; s += 997) {
        graph_format_ago(s, buf, sizeof(buf));
        if (strlen(buf) > 5) { all_short = 0; }
    }
    ok(all_short, "relative labels stay <= 5 characters");
}


/* The scaling path a real screen walks, end to end: a known series into a
 * known box, with the resulting pixel rows checked against hand arithmetic.
 * Catches a sign flip or an off-by-one in the composition of the pieces that
 * the per-function tests above would each pass individually. */
static void test_end_to_end(void)
{
    printf("end to end (battery-shaped series)\n");

    struct graph_box outer = { 4, 4, 124, 156 };
    struct graph_box plot = graph_plot_rect(outer, 32, 14);

    /* A day of battery: starts at 4.10 V, sags to 3.90 V, one 3.85 V dip. */
    int16_t series[288];
    for (size_t i = 0; i < 288; i++) {
        series[i] = (int16_t)(4100 - (int)(i * 200 / 288));
    }
    series[100] = 3850;   /* the dip that column aggregation must preserve */

    int16_t lo = 0, hi = 0;
    graph_minmax(series, 288, &lo, &hi);
    eq_i(lo, 3850, "the dip is the minimum");
    eq_i(hi, 4100, "the start is the maximum");

    eq_i(graph_value_to_y(hi, lo, hi, plot.top, plot.bottom), plot.top,
         "max plots at the top of the plot rect");
    eq_i(graph_value_to_y(lo, lo, hi, plot.top, plot.bottom), plot.bottom,
         "min plots at the bottom of the plot rect");

    /* The dip must survive aggregation: find the column covering sample 100
     * and confirm its min is the dip value, i.e. that drawGraphEx would draw
     * a run reaching it. This is the property that plain sub-sampling breaks. */
    uint16_t cols = (uint16_t)(plot.right - plot.left + 1);
    int found = 0;
    for (uint16_t c = 0; c < cols; c++) {
        size_t from, to;
        graph_column_span(288, cols, c, &from, &to);
        if (100 >= from && 100 < to) {
            int16_t col_lo = series[from];
            for (size_t i = from + 1; i < to; i++) {
                if (series[i] < col_lo) { col_lo = series[i]; }
            }
            eq_i(col_lo, 3850, "the dip survives column aggregation");
            found = 1;
            break;
        }
    }
    ok(found, "some column covers the dip");
}


int main(void)
{
    test_plot_rect();
    test_value_to_y();
    test_col_to_x();
    test_column_span();
    test_minmax_and_range();
    test_labels();
    test_end_to_end();

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    if (failures == 0) {
        printf("ALL PASS\n");
    }
    return failures ? 1 : 0;
}
