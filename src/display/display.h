//*****************************************************************************
//!
//! @file display.h
//! @author Anders Bandt
//! @brief This file is defining functions to control a display
//! @version 1.0
//! @date August 9th, 2024
//!
//*****************************************************************************

#ifndef SRC_HARDWARE_DISPLAY_H_
#define SRC_HARDWARE_DISPLAY_H_


/* standard C file */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>


extern int display_status;


// Font size enum
typedef enum {
    FONT_SMALL = 12,   // ter_u12b
    FONT_MEDIUM = 16,  // ter_u16b
    FONT_LARGE = 20,   // ter_u20b
    FONT_XLARGE = 24,  // ter_u24b (default)
    FONT_XXLARGE = 28, // ter_u28b
    FONT_HUGE = 32     // ter_u32b
} font_size_t;

// ST7735S line Y positions
#define line1_Y 35
#define line2_Y 55
#define line3_Y 95
#define line4_Y 125

#include <st7735s.h>
#include <display/graph_layout.h>
#include <gfx.h>
#include <fonts.h>


/**
 * @brief initializes the display
 *
 * @returns None (void)
 */
void init_display();


/**
 * @brief clears all content on the display
 *
 * @returns None (void)
 */
void clear_display();


/**
 * @brief Switch the screen's theme — ground AND ink together.
 *
 * Affects clear_display() AND every subsequent draw, because each
 * printLine()/printField*()/printStatusField() clears its own box and then
 * draws its own text — filling the screen alone would leave those punching the
 * old colours back through on the next redraw.
 *
 * Two fixed themes rather than an arbitrary colour on purpose: ground and ink
 * have to move as a pair, and the menu theme is deliberately black-on-light
 * for sunlight legibility while the clock face stays light-on-dark. Keeping
 * both inside display.c keeps every caller out of the palette.
 *
 * Call display_set_default_background() when returning to the clock face.
 */
void display_set_menu_background(void);
void display_set_default_background(void);


/**
 * @brief Prints text to a predefined line on the display with specified font size
 *
 * @param text: pointer to char for the string. String should terminate in \0
 *
 * @param lineNum: line number to print to (0-4)
 *
 * @param posX: x position to start printing to
 *
 * @param fontSize: font size to use (FONT_SMALL, FONT_MEDIUM, FONT_LARGE, etc.)
 */
void printLine(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize);


/**
 * @brief Clears the text area with background color, then prints text to a line
 *
 * @param text: pointer to char for the string. String should terminate in \0
 *
 * @param lineNum: line number to print to (0-4)
 *
 * @param posX: x position to start printing to
 *
 * @param fontSize: font size to use (FONT_SMALL, FONT_MEDIUM, FONT_LARGE, etc.)
 */
void clearAndPrintLine(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize);


/**
 * @brief Right-aligns text within a fixed-size box, always clearing the whole box first
 *
 * Unlike clearAndPrintLine(), the cleared region never depends on the current text's
 * length — safe for small corner badges (e.g. temp, step count) whose digit count
 * changes over time.
 *
 * @param text: pointer to char for the string. String should terminate in \0
 * @param posY: pixel Y position (top of the text)
 * @param fieldRight: pixel X of the box's right edge
 * @param fieldWidth: box width in pixels — must comfortably fit the longest expected text
 * @param fontSize: font size to use
 */
void printFieldRightAligned(const char * text, const uint32_t posY, const uint32_t fieldRight,
                             const uint32_t fieldWidth, font_size_t fontSize);


/**
 * @brief Y position for a given line/font, as used internally by printLine() etc.
 *
 * Exposed so callers computing their own fixed-box field geometry (see
 * printTwoDigitFieldIfChanged()) don't have to hand-duplicate this math.
 *
 * @param lineNum: line number (0-4)
 * @param fontSize: font size to use
 */
uint32_t calculateLineY(uint32_t lineNum, font_size_t fontSize);


/**
 * @brief Draws a small on/off status indicator in a fixed right-aligned box
 *
 * Same geometry and clear-first behavior as printFieldRightAligned(), but the
 * text is drawn in the accent color when @p active and the dim color when not —
 * for constant-text badges whose meaning is carried by their state rather than
 * their content (clock face charging / low-power indicators).
 *
 * @param text: pointer to char for the string. String should terminate in \0
 * @param posY: pixel Y position (top of the text)
 * @param fieldRight: pixel X of the box's right edge
 * @param fieldWidth: box width in pixels
 * @param fontSize: font size to use
 * @param active: true to draw in the accent color, false for dim
 */
void printStatusField(const char * text, const uint32_t posY, const uint32_t fieldRight,
                      const uint32_t fieldWidth, font_size_t fontSize, bool active);


/**
 * @brief Draws the wear indicator: a green check (worn) or a red cross (not).
 *
 * Same fixed-box geometry as printStatusField() — same 2 px pad, same
 * clear-first, same off-screen band — so it lines up with the "BT"/"LP"/"CX"
 * text badges and cannot leave stale pixels behind. The difference is that
 * the state changes the GLYPH here, not just the color, which is why the box
 * must be (and is) cleared in full on every call.
 *
 * The glyph is a square of side `fontSize`, right-aligned in the field.
 *
 * @param posY: pixel Y position (top of the glyph box)
 * @param fieldRight: pixel X of the box's right edge
 * @param fieldWidth: box width in pixels
 * @param fontSize: sets the glyph's side length, to match neighbouring text
 * @param worn: true for the green check, false for the red cross
 */
void printWearField(const uint32_t posY, const uint32_t fieldRight,
                    const uint32_t fieldWidth, font_size_t fontSize, bool worn);


/**
 * @brief Redraws a right-aligned "%02u" field only if it changed since last_value
 *
 * Thin wrapper around printFieldRightAligned() that skips the redraw (and the SPI
 * traffic it costs) when the value hasn't changed. Shared by the wall-clock
 * HH:MM:SS display and the stopwatch MM:SS display so both fields update at
 * the same granularity instead of redrawing whole multi-digit strings for a
 * single digit's change.
 *
 * @param value: field value to display (0-99 for a %02u field)
 * @param last_value: caller-owned last-drawn value; pass -1 initially to force the first draw
 * @param posY: pixel Y position (top of the text)
 * @param fieldRight: pixel X of the box's right edge
 * @param fieldWidth: box width in pixels
 * @param fontSize: font size to use
 */
void printTwoDigitFieldIfChanged(uint32_t value, int32_t *last_value,
                                  const uint32_t posY, const uint32_t fieldRight,
                                  const uint32_t fieldWidth, font_size_t fontSize);


/**
 * @brief Left-aligns text within a fixed-size box, always clearing the whole box first
 *
 * Mirror of printFieldRightAligned() for corner badges anchored on the left edge —
 * text position is already stable for left-aligned strings, but the clear region is
 * still bounded to fieldLeft..fieldLeft+fieldWidth (not the full row) so a shrinking
 * value can't leave stale trailing digits while also not trampling anything else
 * sharing that row further right (e.g. a badge in the opposite corner).
 *
 * @param text: pointer to char for the string. String should terminate in \0
 * @param posY: pixel Y position (top of the text)
 * @param fieldLeft: pixel X of the box's left edge
 * @param fieldWidth: box width in pixels — must comfortably fit the longest expected text
 * @param fontSize: font size to use
 */
void printFieldLeftAligned(const char * text, const uint32_t posY, const uint32_t fieldLeft,
                            const uint32_t fieldWidth, font_size_t fontSize);


/* Axis label metrics. The font is the smallest we have because three x labels
 * have to share a ~92 px plot at 6 px per character; the y margin fits five
 * characters ("100%%", "4.20", "-12.3") plus a pixel of air. */
#define GRAPH_AXIS_FONT   FONT_SMALL
#define GRAPH_Y_LABEL_W   32

typedef enum {
    GRAPH_LINE = 0,   /* polyline — a value sampled at instants */
    GRAPH_BAR,        /* bars from a zero baseline — a rate over an interval */
} graph_style_t;

/*
 * Optional decoration for drawGraphEx(). Every field may be left zero/NULL,
 * in which case that element is not drawn AND its margin is not reserved —
 * a graph with no labels gets the full box to plot in.
 *
 * Labels are caller-formatted strings rather than values plus a format spec
 * on purpose: the three graphs that exist want degrees, millivolts and step
 * counts, with different precisions and units, and a formatting mini-language
 * inside the drawing primitive would be larger than the three snprintf()s it
 * replaced.
 */
struct graph_opts {
    graph_style_t style;

    const char *y_max_label;   /* drawn in the left margin, at the plot's top */
    const char *y_mid_label;
    const char *y_min_label;

    const char *x_left;        /* drawn under the plot: oldest ... newest */
    const char *x_mid;
    const char *x_right;

    int16_t mark_column;       /* column to outline instead of fill, or -1 */

    /* Draw a horizontal rule at value 0 across the plot. For a signed
     * quantity (angular rate) the sign is most of the meaning, and without a
     * marked zero an auto-ranged box cannot tell "spinning one way" from
     * "spinning the other". Ignored when 0 is outside [y_min, y_max]. */
    bool zero_line;
};


/*
 * One plotted series for drawGraphMulti(). Colour is per-trace because the
 * only reason to overlay series at all is to compare them, and three
 * identically-coloured lines are less readable than one.
 *
 * Components are the 5/6/5 ranges setColor() takes (r 0-31, g 0-63, b 0-31),
 * in honest RGB order. Leave `color` all-zero to plot in the theme's
 * foreground ink.
 */
struct graph_trace {
    const int16_t *data;
    uint8_t color[3];
};


/**
 * @brief Draws a graph, with axis labels, into a box it owns entirely
 *
 * Scales num_data samples into [y_min, y_max] (values outside are clamped,
 * not auto-ranged), clears the whole outer box first, reserves margins for
 * whichever labels were supplied, and flushes once at the end regardless of
 * sample count. Aggregates min..max per column when there are more samples
 * than pixels, so nothing is dropped.
 *
 * @param data: sample array, left to right (oldest to newest)
 * @param num_data: sample count; 1 draws a single tick, 0 is a no-op
 * @param y_min: value mapped to the bottom of the plot
 * @param y_max: value mapped to the top of the plot (must exceed y_min)
 * @param box: the whole rect to draw into, axis margins included
 * @param opts: style and labels; NULL means a bare unlabelled line graph
 */
void drawGraphEx(const int16_t *data, size_t num_data, int16_t y_min, int16_t y_max,
                 struct graph_box box, const struct graph_opts *opts);


/**
 * @brief Draws several series over one shared set of axes
 *
 * Same contract as drawGraphEx() — which is now a one-trace call into this —
 * except that every trace shares the box, the y range, the labels and the
 * single clear, so they can be compared against each other rather than
 * against three different auto-ranges. All traces must be num_data long.
 *
 * One clear and one band for the whole figure is the reason this exists as a
 * primitive rather than as three drawGraphEx() calls: the second call would
 * clear away the first trace, and its band would composite over it.
 *
 * @param traces: per-series data + colour
 * @param num_traces: how many; 0 draws an empty framed box
 * @param num_data: samples per trace, left to right (oldest to newest)
 */
void drawGraphMulti(const struct graph_trace *traces, size_t num_traces, size_t num_data,
                    int16_t y_min, int16_t y_max, struct graph_box box,
                    const struct graph_opts *opts);


/**
 * @brief Draws a primitive line graph over a fixed pixel box
 *
 * Thin wrapper over drawGraphEx() with no labels — the plot fills the box.
 *
 * @param data: sample array, left to right
 * @param num_data: sample count, must be >= 2 (no-op otherwise)
 * @param y_min: value mapped to the bottom of the box
 * @param y_max: value mapped to the top of the box
 * @param left: pixel X of the box's left edge
 * @param top: pixel Y of the box's top edge
 * @param right: pixel X of the box's right edge
 * @param bottom: pixel Y of the box's bottom edge
 */
void drawGraph(const int16_t *data, size_t num_data, int16_t y_min, int16_t y_max,
               uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);


/**
 * @brief Selects a font face for subsequent raw drawText() calls
 *
 * For screens drawing their own coloured text; the print* helpers below pick
 * a face themselves and need no call to this.
 */
void display_set_font(font_size_t fontSize);


/**
 * @brief Draws one signed value as a centre-zero bar gauge plus its number
 *
 * The live IMU screen's accelerometer rows. Zero sits at the middle of the
 * track, so sign and magnitude are both readable without parsing digits;
 * @p value beyond @p full_scale clamps rather than auto-ranging.
 *
 * Fits (and expects) one row at a time — the whole three-axis block would not
 * fit the off-screen composite band this uses to draw without flicker.
 *
 * @param left,top,right,bottom: inclusive pixel rect for the whole row
 * @param label: one-character axis name drawn at the left, or NULL
 * @param value: signed value, in the same unit as full_scale (milli-units)
 * @param full_scale: value mapped to a full half-track; must be > 0
 * @param color: bar colour, setColor()'s 5/6/5 RGB
 */
void drawAxisGauge(int16_t left, int16_t top, int16_t right, int16_t bottom,
                   const char *label, int32_t value, int32_t full_scale,
                   const uint8_t color[3]);


/**
 * @brief Draws a green (on) or red (off) border around the whole panel
 *
 * The state indicator for every binary setting screen — a colour at the edge
 * of the screen says on/off without the reader having to decode a 1 or a 0.
 * Draw it before the screen's text; it does not clear the interior.
 *
 * @param on: true for the green "enabled" ring, false for the red one
 * @param thickness: border width in pixels (clamped to HEIGHT/4)
 */
void drawStatusRing(bool on, uint8_t thickness);


/**
 * @brief Reads back the current foreground/background colours
 *
 * For screens composing their own widgets from gfx primitives, which would
 * otherwise have to guess whether the clock-face or the menu ground is in
 * effect. Either pointer may be NULL.
 */
void display_theme_colors(uint8_t fore[3], uint8_t back[3]);


/**
 * @brief Prints text to a line without drawing background (transparent mode)
 *
 * @param text: pointer to char for the string. String should terminate in \0
 *
 * @param lineNum: line number to print to (0-4)
 *
 * @param posX: x position to start printing to
 *
 * @param fontSize: font size to use (FONT_SMALL, FONT_MEDIUM, FONT_LARGE, etc.)
 */
void printLineTransparent(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize);


/**
 * @brief Handles printing text to whatever display is connected
 *
 * @param text: pointer to char for the string. String should terminate in \0
 *
 * @param posX: the x position to start printing the text to the screen
 *
 * @param posY: the y position to start printing the text to the screen
 */
void printToScreenInverted(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize);


/**
 * @brief Prints text to a line with selective character inversion
 *
 * @param text: pointer to char for the string. String should terminate in \0
 *
 * @param lineNum: line number to print to (0-4)
 *
 * @param posX: x position to start printing to
 *
 * @param fontSize: font size to use (FONT_SMALL, FONT_MEDIUM, FONT_LARGE, etc.)
 *
 * @param invertStart: starting index of characters to invert (inclusive)
 *
 * @param invertEnd: ending index of characters to invert (inclusive)
 */
void printLineWithInversion(const char * text, const uint32_t lineNum, const uint32_t posX,
                            font_size_t fontSize, int invertStart, int invertEnd);




void changeContrast(const uint8_t contrast);


void switch_display(const bool on);

/**
 * @brief Fade the backlight to black over duration_ms, then leave it dark.
 *
 * Used by the display auto-off so the screen dims away instead of snapping
 * off. Only animates the backlight PWM — it does not sleep the panel, and it
 * does not take display_draw_mutex (no SPI involved), so drawing threads keep
 * running throughout. See the implementation comment in display.c for why this
 * is a backlight ramp rather than a per-pixel dissolve, and why the ramp is
 * quadratic.
 *
 * Crucially it does not disturb the remembered backlight level, so waking the
 * panel later restores the user's brightness rather than coming back black.
 *
 * @param duration_ms total fade time; a duration shorter than one step just
 *                    goes dark immediately
 * @param cancelled   polled once per step (~25 ms); return true to abort. Pass
 *                    NULL for an uninterruptible fade.
 * @return true if the fade completed and the screen is now dark; false if it
 *         was cancelled, in which case the backlight is already back at its
 *         previous level and the caller should remain awake.
 */
bool display_fade_out(uint32_t duration_ms, bool (*cancelled)(void));

/**
 * @brief Returns true if the display is currently awake (sleep-out/active).
 */
bool display_is_awake(void);



#endif /* SRC_HARDWARE_DISPLAY_H_ */
