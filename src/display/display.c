//*****************************************************************************
//!
//! @file display.c
//! @author Anders Bandt
//! @brief This file describes functions for controlling the main display
//! @version 1.0
//! @date August 9th, 2024
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* standard C file */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>


/* My header files */
#include <display.h>
#include <display/wear_glyph.h>


LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);


/* ---- palette -------------------------------------------------------------
 *
 * All constants below are TRUE RGB565 channel values (r 0..31, g 0..63,
 * b 0..31) in setColor()/setbgColor() argument order. As of 2026-08-25 that
 * is honest: gfx.c's setColor() does the r/b crossover the packed struct
 * needs, once, so setColor(31,0,0) is red on the glass. The palette used to
 * be written in struct order and hand-swapped at individual call sites — if
 * you are reading an old comment that says a constant "renders as" some other
 * colour, it is stale.
 *
 * Two themes, switched by display_set_menu_background() /
 * display_set_default_background():
 *
 *   clock face  - dark Darcula grey ground, light text  (glanceable indoors,
 *                 and the badges' accent/dim colours are tuned for it)
 *   menu + leaf - light ground, BLACK text              (sunlight)
 *
 * Why the menu is a light theme: this is a transmissive TFT with no
 * transflective layer, so in direct sun a roughly constant veiling glare sits
 * on top of every pixel and swamps the backlight. What survives is the
 * absolute luminance *difference* between ink and ground, so the ground wants
 * to be as bright as the panel can make it and the ink as dark as possible —
 * i.e. black on near-white, not light-on-dark. (Unlike an OLED, a white
 * ground costs no extra power here: the backlight is on regardless.)
 */

/* Clock-face ground: Darcula background #2B2B2B */
#define BACK_R 5
#define BACK_G 10
#define BACK_B 5

/* Clock-face text: #C6B4AD.
 *
 * NOT the Darcula #A9B7C6 the old comment claimed. That constant was written
 * in struct order, so the panel has been showing its r/b mirror — this warm
 * light lilac — for the whole life of the project. It is what Anders means by
 * "our original main display light purple", so it is kept exactly as it
 * appears on the glass and simply written down honestly. */
#define FORE_R 24
#define FORE_G 45
#define FORE_B 21

/* Clock-face badge states (charging / low-power indicators). Only ever drawn
 * on the dark ground, so these do not need light-theme variants. */
#define ACCENT_R 25 // accent (selection/highlight) — Darcula orange #CC7832
#define ACCENT_G 30
#define ACCENT_B  6

#define DIM_R 11    // dim/secondary text #5C6773
#define DIM_G 25
#define DIM_B 14

/* Off-palette on purpose. The Darcula set above has no green and no red, and
 * the wear indicator is the one badge whose meaning is carried by hue rather
 * than by a label — a check and a cross are only legible as "good" and "bad"
 * if they are actually green and red. Kept adjacent to the palette so it is
 * obvious these two are the exception, not a second theme. */
#define GOOD_RED 11   // wear: on-wrist  #58BC58
#define GOOD_GRN 47
#define GOOD_BLU 11

#define BAD_RED  28   // wear: off-wrist #E05450
#define BAD_GRN  21
#define BAD_BLU  10

/* Menu / leaf-screen ground: the clock face's light lilac, per Anders'
 * suggestion — black on it instead of the #F52091 pink, which put low-contrast
 * light text on a mid-luminance ground and would have been unreadable outside.
 * Push this closer to white (e.g. 28/58/28) if the sun still wins; the green
 * channel carries most of the luminance, so brightening g buys the most. */
#define MENU_BACK_R FORE_R
#define MENU_BACK_G FORE_G
#define MENU_BACK_B FORE_B

#define MENU_FORE_R 0   /* black ink — maximum luminance difference */
#define MENU_FORE_G 0
#define MENU_FORE_B 0

/* The CURRENT ground and ink.
 *
 * Everything that erases before drawing paints with ui_back[] rather than with
 * BACK_* directly, and every text draw uses ui_fore[] rather than FORE_*. That
 * is the whole point: it is not enough to fill the screen once on entering a
 * mode, because every printLine()/printField*()/printStatusField() clears its
 * own box first and then draws its own text — each of those would otherwise
 * punch a clock-face-coloured rectangle, in clock-face-coloured text, through
 * a menu screen the moment it redrew. */
static uint8_t ui_back[3] = { BACK_R, BACK_G, BACK_B };
static uint8_t ui_fore[3] = { FORE_R, FORE_G, FORE_B };

void display_set_menu_background(void)
{
    ui_back[0] = MENU_BACK_R;
    ui_back[1] = MENU_BACK_G;
    ui_back[2] = MENU_BACK_B;

    ui_fore[0] = MENU_FORE_R;
    ui_fore[1] = MENU_FORE_G;
    ui_fore[2] = MENU_FORE_B;
}

void display_set_default_background(void)
{
    ui_back[0] = BACK_R;
    ui_back[1] = BACK_G;
    ui_back[2] = BACK_B;

    ui_fore[0] = FORE_R;
    ui_fore[1] = FORE_G;
    ui_fore[2] = FORE_B;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @var Screen definition */
extern struct S_SCREEN Screen;

int display_status;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL FUNCTIONS -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * getFontPointer: returns pointer to font array based on font size enum
 */
static const uint8_t* getFontPointer(font_size_t fontSize)
{
    switch (fontSize) {
        case FONT_SMALL:    return ter_u12b;
        case FONT_MEDIUM:   return ter_u16b;
        case FONT_LARGE:    return ter_u20b;
        case FONT_XLARGE:   return ter_u24b;
        case FONT_XXLARGE:  return ter_u28b;
        case FONT_HUGE:     return ter_u32b;
        default:            return ter_u24b;  // default to XLARGE
    }
}


/*
 * calculateLineY: calculates Y position for a line based on font size
 */
uint32_t calculateLineY(uint32_t lineNum, font_size_t fontSize)
{
    if (lineNum == 0) {
        return 2;  // special case for line 0 - always at top
    }

    // Get font height from the enum value
    uint32_t fontHeight = (uint32_t)fontSize;

    // Add some spacing between lines (30% of font height)
    uint32_t lineSpacing = fontHeight * 3 / 10;

    // Calculate Y position: top margin + (line_number * (font_height + spacing))
    uint32_t topMargin = 10;
    return topMargin + (lineNum * (fontHeight + lineSpacing));
}


/*
 * printToScreen: prints a certain text value to a certain position on the screen
 */
void printToScreen(const char * text, const uint32_t posY, const uint32_t posX, font_size_t fontSize)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    setFont(getFontPointer(fontSize));
    drawText(posX, posY, text);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void init_display() {
    int rc = ST7735S_Init();
    if (rc != 0) {
        LOG_ERR("ST7735S_Init failed with code %d", rc);
        display_status = 0;
        return;
    }
    // setOrientation(R90);

    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    setbgColor(ui_back[0], ui_back[1], ui_back[2]);
    // fillScreen();

    display_status = 1;
}



/**
 * clearDisplay: clears all content on the display
 *
 * *** fillScreen() paints with the FOREGROUND colour, not the background one. ***
 *
 * fillScreen() -> filledRect() -> drawLine() -> ST7735S_Pixel(), and that last
 * one writes gfx.c's `color`. `bg_color` is only ever read by ST7735S_bgPixel(),
 * i.e. by font rendering in non-transparent mode. So the setbgColor() below does
 * NOT decide what the wipe paints — it is here only so text drawn afterwards in
 * opaque mode lands on the right ground.
 *
 * This function used to set only the background and then wipe with whatever
 * foreground the previous screen happened to leave behind. That was wrong the
 * whole time and invisible for just as long, because the leftover was almost
 * always the light body text colour. The moment the menu theme made the ink
 * BLACK (2026-08-26), exiting a menu started wiping the panel black and then
 * drawing correctly-coloured boxes on top of it — the "inverted screen" report.
 *
 * Every other clear in this file already does it correctly (setColor(ui_back)
 * then filledRect); this one is now consistent with them, and restores the ink
 * afterwards so a caller that draws text next is unaffected.
 */
void clear_display()
{
    setColor(ui_back[0], ui_back[1], ui_back[2]);     /* what fillScreen actually paints with */
    setbgColor(ui_back[0], ui_back[1], ui_back[2]);   /* ground for opaque text drawn later */
    fillScreen();

    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);     /* leave the ink where callers expect it */
    flushBuffer();
}


/*
 * printLine: prints an individual line to the screen
 */
void printLine(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // Calculate Y position based on font size and line number
    uint32_t posY = calculateLineY(lineNum, fontSize);
    printToScreen(text, posY, posX, fontSize);

    flushBuffer();
}


/*
 * beginFieldBand: opens an off-screen composite band covering exactly the same
 * rect the caller is about to clear with filledRect(), so the clear and the
 * text that lands on top of it reach the panel as one transfer instead of two
 * visible passes. That two-pass sequence is what the field flicker actually
 * is: the panel shows the emptied box for the duration of the glyph rendering.
 *
 * Takes filledRect()'s inclusive corner convention and converts to the
 * width/height the driver wants, clamping at 0 first — the callers below
 * routinely compute `posX - 2`, which wraps if the field starts at column 0
 * or 1, and an unsigned wrap here would ask for an absurd rect.
 *
 * Returns false when the rect won't fit the band (ST7735S_BAND_ROWS) or the
 * band is otherwise unavailable; the caller then draws exactly as it did
 * before, correctly, just unbuffered. Only call endFieldBand() — via
 * endBand() — when this returned true.
 */
static bool beginFieldBand(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    return beginBand((uint16_t)x0, (uint16_t)y0,
                     (uint16_t)(x1 - x0 + 1), (uint16_t)(y1 - y0 + 1));
}

/*
 * clearAndPrintLine: clears the text area with background color, then prints text to the line
 */
void clearAndPrintLine(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // Calculate Y position based on font size and line number
    uint32_t posY = calculateLineY(lineNum, fontSize);
    uint32_t fontHeight = (uint32_t)fontSize;

    bool banded = beginFieldBand((int32_t)posX - 2, (int32_t)posY - 2,
                                 127, (int32_t)(posY + fontHeight + 2));

    // Clear the area with background color
    setColor(ui_back[0], ui_back[1], ui_back[2]);
    filledRect(posX - 2, posY - 2, 127, posY + fontHeight + 2);

    // Draw the text in white
    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    printToScreen(text, posY, posX, fontSize);

    flushBuffer();
    if (banded) {
        endBand();
    }
}


/*
 * printFieldRightAligned: right-aligns text within a FIXED-size box (posY, fieldRight,
 * fieldWidth all caller-chosen and constant across calls) and always clears that whole
 * box before drawing. Unlike clearAndPrintLine() (which clears from the current text's
 * left edge to the screen's right edge), this is safe for corner badges whose content
 * changes width call to call — e.g. "100" -> "9" — since the clear region never depends
 * on the current text's length, only on the fixed box geometry.
 */
void printFieldRightAligned(const char * text, const uint32_t posY, const uint32_t fieldRight,
                             const uint32_t fieldWidth, font_size_t fontSize)
{
    if (text == 0) {
        return;
    }

    uint32_t fontHeight = (uint32_t)fontSize;
    uint32_t charWidth = fontSize / 2;  // approximation, matches printLineWithInversion's convention
    uint32_t textWidth = strlen(text) * charWidth;
    uint32_t fieldLeft = fieldRight - fieldWidth;
    uint32_t textX = (textWidth < fieldWidth) ? (fieldRight - textWidth) : fieldLeft;

    bool banded = beginFieldBand((int32_t)fieldLeft - 2, (int32_t)posY - 2,
                                 (int32_t)(fieldRight + 2),
                                 (int32_t)(posY + fontHeight + 2));

    setColor(ui_back[0], ui_back[1], ui_back[2]);
    filledRect(fieldLeft - 2, posY - 2, fieldRight + 2, posY + fontHeight + 2);

    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    setFont(getFontPointer(fontSize));
    drawText(textX, posY, text);

    flushBuffer();
    if (banded) {
        endBand();
    }
}


/*
 * printStatusField: same fixed-box, right-aligned, always-clear-first geometry as
 * printFieldRightAligned(), but drawn in the accent color when `active` and the dim
 * color when not — for small on/off status indicators (e.g. the clock face's
 * charging / low-power badges) where the text is constant and only its *state*
 * changes. Kept separate rather than adding a color argument to
 * printFieldRightAligned() so every existing caller of that function keeps its
 * "always FORE" behavior unchanged.
 *
 * Restores the shared FORE color before returning — setColor() is global state in
 * gfx.c, so leaving it on ACCENT/DIM would silently recolor whatever drew next.
 */
void printStatusField(const char * text, const uint32_t posY, const uint32_t fieldRight,
                      const uint32_t fieldWidth, font_size_t fontSize, bool active)
{
    if (text == 0) {
        return;
    }

    uint32_t fontHeight = (uint32_t)fontSize;
    uint32_t charWidth = fontSize / 2;  /* matches printFieldRightAligned's convention */
    uint32_t textWidth = strlen(text) * charWidth;
    uint32_t fieldLeft = fieldRight - fieldWidth;
    uint32_t textX = (textWidth < fieldWidth) ? (fieldRight - textWidth) : fieldLeft;

    bool banded = beginFieldBand((int32_t)fieldLeft - 2, (int32_t)posY - 2,
                                 (int32_t)(fieldRight + 2),
                                 (int32_t)(posY + fontHeight + 2));

    setColor(ui_back[0], ui_back[1], ui_back[2]);
    filledRect(fieldLeft - 2, posY - 2, fieldRight + 2, posY + fontHeight + 2);

    if (active) {
        setColor(ACCENT_R, ACCENT_G, ACCENT_B);
    } else {
        setColor(DIM_R, DIM_G, DIM_B);
    }
    setFont(getFontPointer(fontSize));
    drawText(textX, posY, text);

    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    flushBuffer();
    if (banded) {
        endBand();
    }
}


/*
 * printWearField: the clock face's wear indicator — a green check when the
 * watch is on a wrist, a red cross when it is not.
 *
 * Lives here rather than in ui_display.c for the reason already recorded on
 * CLOCK_BLE_RIGHT over there: the palette constants and beginFieldBand() are
 * static to this file, so anything that wants to draw a colored glyph in a
 * badge box either lives here or duplicates the palette across a module
 * boundary. This is the first caller to actually need that, and it is the
 * same door the Bluetooth rune should come through later.
 *
 * Geometry is deliberately identical to printStatusField() — same fixed box,
 * same 2 px pad, same clear-first, same band — so this badge lines up with
 * the "BT"/"LP"/"CX" text badges and can never leave stale pixels behind.
 * Unlike those, the STATE here changes the glyph as well as the color; that
 * is safe only because the box is cleared in full every call.
 *
 * The glyph is a square of side `fontSize` right-aligned in the field, so it
 * matches the cap height of the text badges beside and below it. The stroke
 * geometry itself lives in wear_glyph.h rather than here, so that the
 * host-side renderer in test/band/ draws the same shape this does — see that
 * header for why, and run `make -C test/band run-wear` after touching it.
 */
void printWearField(const uint32_t posY, const uint32_t fieldRight,
                    const uint32_t fieldWidth, font_size_t fontSize, bool worn)
{
    uint32_t fontHeight = (uint32_t)fontSize;
    uint32_t fieldLeft  = fieldRight - fieldWidth;

    bool banded = beginFieldBand((int32_t)fieldLeft - 2, (int32_t)posY - 2,
                                 (int32_t)(fieldRight + 2),
                                 (int32_t)(posY + fontHeight + 2));

    setColor(ui_back[0], ui_back[1], ui_back[2]);
    filledRect(fieldLeft - 2, posY - 2, fieldRight + 2, posY + fontHeight + 2);

    /* Square glyph box, right-aligned in the field and inset by 1 px so the
     * strokes (and their +1 px thickening) stay inside the cleared area. */
    uint32_t side = fontHeight;
    uint32_t x1   = fieldRight - 1;
    uint32_t x0   = (x1 > fieldLeft + side) ? (x1 - side) : fieldLeft;

    setColor(worn ? GOOD_RED : BAD_RED,
             worn ? GOOD_GRN : BAD_GRN,
             worn ? GOOD_BLU : BAD_BLU);

    wear_glyph_strokes(x0, posY, side, worn, drawLine);

    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    flushBuffer();
    if (banded) {
        endBand();
    }
}


/*
 * printTwoDigitFieldIfChanged: redraws a right-aligned "%02u" field via
 * printFieldRightAligned() only when value differs from *last_value, and
 * updates *last_value to match. Pass -1 as the initial *last_value to force
 * the first call to draw.
 *
 * Factored out of display_out_time()'s HH/MM/SS partial-redraw so the
 * wall-clock face and the stopwatch's MM:SS can share one diffing
 * implementation instead of each hand-rolling the same "if changed, redraw
 * just this field" logic.
 */
void printTwoDigitFieldIfChanged(uint32_t value, int32_t *last_value,
                                  const uint32_t posY, const uint32_t fieldRight,
                                  const uint32_t fieldWidth, font_size_t fontSize)
{
    if ((int32_t)value == *last_value) {
        return;
    }

    char field[12];  /* "%02u" of a uint32_t - documented for 0-99 fields but sized
                       * for the full range defensively, since this is a public helper */
    sprintf(field, "%02u", value);
    printFieldRightAligned(field, posY, fieldRight, fieldWidth, fontSize);
    *last_value = (int32_t)value;
}


/*
 * printFieldLeftAligned: left-aligns text within a FIXED-size box (posY, fieldLeft,
 * fieldWidth all caller-chosen and constant across calls) and always clears that whole
 * box before drawing. Mirror of printFieldRightAligned() — see display.h.
 */
void printFieldLeftAligned(const char * text, const uint32_t posY, const uint32_t fieldLeft,
                            const uint32_t fieldWidth, font_size_t fontSize)
{
    if (text == 0) {
        return;
    }

    uint32_t fontHeight = (uint32_t)fontSize;

    bool banded = beginFieldBand((int32_t)fieldLeft - 2, (int32_t)posY - 2,
                                 (int32_t)(fieldLeft + fieldWidth + 2),
                                 (int32_t)(posY + fontHeight + 2));

    setColor(ui_back[0], ui_back[1], ui_back[2]);
    filledRect(fieldLeft - 2, posY - 2, fieldLeft + fieldWidth + 2, posY + fontHeight + 2);

    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    setFont(getFontPointer(fontSize));
    drawText(fieldLeft, posY, text);

    flushBuffer();
    if (banded) {
        endBand();
    }
}


/*
 * drawGraph: thin wrapper kept for callers that just want a bare polyline in
 * a box — see drawGraphEx() below, which is where the work is now.
 */
void drawGraph(const int16_t *data, size_t num_data, int16_t y_min, int16_t y_max,
               uint16_t left, uint16_t top, uint16_t right, uint16_t bottom)
{
    struct graph_opts opts = { .style = GRAPH_LINE };
    struct graph_box box = { (int16_t)left, (int16_t)top, (int16_t)right, (int16_t)bottom };

    drawGraphEx(data, num_data, y_min, y_max, box, &opts);
}


/*
 * drawGraphEx: the graph primitive. See display.h for the parameter contract
 * and graph_layout.h for the arithmetic, which lives outside this file so it
 * can be tested on a host with no board attached (test/band/graph_test.c).
 *
 * Three things this does that the original drawGraph() did not, each of which
 * came from a real gap rather than from wanting a richer widget:
 *
 * IT OWNS ITS MARGINS. The caller hands over one outer rect and this reserves
 * space inside it for axis labels. The temperature screen used to compute the
 * label margin itself and pass the reduced box, which left a strip of the
 * previous screen visible beside the plot until that screen learned to clear
 * itself first. One owner, one clear, no strip.
 *
 * IT AGGREGATES WHEN SAMPLES OUTNUMBER PIXELS. A day of battery history is
 * 288 points and the plot is ~90 px wide. Plotting every Nth point would drop
 * exactly the excursions worth seeing, so each column draws its samples'
 * min..max as a vertical run — dense regions read as a band, and a one-sample
 * sag stays visible.
 *
 * IT DRAWS BARS. Steps per hour is a rate over an interval, not a value at an
 * instant; joining hourly totals with a line implies a continuity that is not
 * there. Bars from a zero baseline say what the data means.
 *
 * Cost discipline is unchanged from the original: everything reuses
 * drawLine()/filledRect()/drawRect() from gfx.c, and the whole graph flushes
 * once at the end rather than once per segment.
 */
void drawGraphEx(const int16_t *data, size_t num_data, int16_t y_min, int16_t y_max,
                 struct graph_box box, const struct graph_opts *opts)
{
    static const struct graph_opts default_opts = { .style = GRAPH_LINE };

    if (opts == NULL) {
        opts = &default_opts;
    }
    if (data == NULL || num_data == 0 || y_max <= y_min ||
        box.right <= box.left || box.bottom <= box.top) {
        return;
    }

    bool has_y_labels = (opts->y_max_label != NULL) || (opts->y_min_label != NULL) ||
                        (opts->y_mid_label != NULL);
    bool has_x_labels = (opts->x_left != NULL) || (opts->x_mid != NULL) ||
                        (opts->x_right != NULL);

    int16_t y_label_w = has_y_labels ? GRAPH_Y_LABEL_W : 0;
    int16_t x_label_h = has_x_labels ? (int16_t)(GRAPH_AXIS_FONT + 2) : 0;

    struct graph_box plot = graph_plot_rect(box, y_label_w, x_label_h);
    if (plot.right <= plot.left || plot.bottom <= plot.top) {
        return;
    }

    /* Attempted for the whole outer rect, not just the plot: the labels are
     * part of the same repaint, and a band that covers only the plot would
     * composite the line while the labels still flickered in beside it.
     * Usually taller than the band and so a no-op, which is fine. */
    bool banded = beginFieldBand(box.left, box.top, box.right, box.bottom);

    setColor(ui_back[0], ui_back[1], ui_back[2]);
    filledRect((uint16_t)box.left, (uint16_t)box.top,
               (uint16_t)box.right, (uint16_t)box.bottom);

    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    drawRect((uint16_t)plot.left, (uint16_t)plot.top,
             (uint16_t)plot.right, (uint16_t)plot.bottom);

    /* Columns: one per sample while they fit, capped at the pixel width once
     * they do not. +1 because the rect bounds are inclusive. */
    uint16_t max_cols = (uint16_t)(plot.right - plot.left + 1);
    uint16_t cols = (num_data < max_cols) ? (uint16_t)num_data : max_cols;

    if (opts->style == GRAPH_BAR) {
        /* Bars sit on the axis, so the baseline is 0 (or y_min if the data
         * never reaches 0) rather than the bottom of an auto-ranged box —
         * a bar whose height is measured from an arbitrary floor is not a
         * bar, it is a misleading line. */
        int32_t baseline_v = (y_min > 0) ? y_min : 0;
        int16_t baseline_y = graph_value_to_y(baseline_v, y_min, y_max,
                                              plot.top, plot.bottom);

        /* Leave a pixel of air between bars once there is room for it, so 24
         * hourly bars read as 24 things and not as one filled region. */
        int16_t bar_w = (int16_t)((plot.right - plot.left) / (cols > 0 ? cols : 1));
        if (bar_w < 1) {
            bar_w = 1;
        } else if (bar_w > 2) {
            bar_w = (int16_t)(bar_w - 1);
        }

        for (uint16_t c = 0; c < cols; c++) {
            size_t from, to;
            graph_column_span(num_data, cols, c, &from, &to);

            int16_t peak = data[from];
            for (size_t i = from + 1; i < to; i++) {
                if (data[i] > peak) {
                    peak = data[i];
                }
            }

            int16_t x0 = graph_col_to_x(c, cols, plot.left, plot.right);
            int16_t y = graph_value_to_y(peak, y_min, y_max, plot.top, plot.bottom);

            if (y >= baseline_y) {
                continue;   /* nothing to show for this column */
            }

            /* Highlight one column if the caller asked (the hour still being
             * filled, which would otherwise be indistinguishable from a quiet
             * one). Drawn as an outline so it reads as "in progress" rather
             * than as a taller bar. */
            bool marked = (opts->mark_column >= 0) && ((uint16_t)opts->mark_column == c);

            for (int16_t dx = 0; dx < bar_w && (x0 + dx) <= plot.right; dx++) {
                if (marked && dx > 0 && dx < bar_w - 1) {
                    setPixel((uint16_t)(x0 + dx), (uint16_t)y);
                    continue;
                }
                drawLine((uint16_t)(x0 + dx), (uint16_t)y,
                         (uint16_t)(x0 + dx), (uint16_t)baseline_y);
            }
        }
    } else if (num_data == 1) {
        /* One sample is not a line. Draw it as a tick at its own height so the
         * screen shows the reading it has rather than nothing at all. */
        int16_t y = graph_value_to_y(data[0], y_min, y_max, plot.top, plot.bottom);
        drawLine((uint16_t)plot.left, (uint16_t)y, (uint16_t)plot.right, (uint16_t)y);
    } else if (num_data <= cols) {
        /* Fewer samples than pixels: a plain polyline, exactly as before. */
        for (size_t i = 0; i + 1 < num_data; i++) {
            int16_t x0 = graph_col_to_x((uint16_t)i, (uint16_t)num_data, plot.left, plot.right);
            int16_t x1 = graph_col_to_x((uint16_t)(i + 1), (uint16_t)num_data, plot.left, plot.right);
            int16_t y0 = graph_value_to_y(data[i], y_min, y_max, plot.top, plot.bottom);
            int16_t y1 = graph_value_to_y(data[i + 1], y_min, y_max, plot.top, plot.bottom);
            drawLine((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
        }
    } else {
        /* More samples than pixels: min..max per column, joined to the
         * previous column so the trace stays continuous across a step. */
        int16_t prev_y = 0;
        bool have_prev = false;

        for (uint16_t c = 0; c < cols; c++) {
            size_t from, to;
            graph_column_span(num_data, cols, c, &from, &to);

            int16_t lo = data[from];
            int16_t hi = data[from];
            for (size_t i = from + 1; i < to; i++) {
                if (data[i] < lo) { lo = data[i]; }
                if (data[i] > hi) { hi = data[i]; }
            }

            int16_t x = graph_col_to_x(c, cols, plot.left, plot.right);
            int16_t y_lo = graph_value_to_y(lo, y_min, y_max, plot.top, plot.bottom);
            int16_t y_hi = graph_value_to_y(hi, y_min, y_max, plot.top, plot.bottom);

            drawLine((uint16_t)x, (uint16_t)y_hi, (uint16_t)x, (uint16_t)y_lo);

            if (have_prev) {
                int16_t x_prev = graph_col_to_x((uint16_t)(c - 1), cols, plot.left, plot.right);
                drawLine((uint16_t)x_prev, (uint16_t)prev_y, (uint16_t)x, (uint16_t)y_hi);
            }
            prev_y = y_lo;
            have_prev = true;
        }
    }

    /* Labels last: drawGraphEx clears its whole rect at the top, so anything
     * drawn before the plot would be wiped, and anything drawn by the CALLER
     * beforehand would be too. */
    if (has_y_labels) {
        setFont(getFontPointer(GRAPH_AXIS_FONT));
        setColor(ui_fore[0], ui_fore[1], ui_fore[2]);

        if (opts->y_max_label != NULL) {
            drawText((uint16_t)box.left, (uint16_t)plot.top, opts->y_max_label);
        }
        if (opts->y_mid_label != NULL) {
            int16_t mid = (int16_t)(plot.top + (plot.bottom - plot.top) / 2 - GRAPH_AXIS_FONT / 2);
            drawText((uint16_t)box.left, (uint16_t)mid, opts->y_mid_label);
        }
        if (opts->y_min_label != NULL) {
            drawText((uint16_t)box.left, (uint16_t)(plot.bottom - GRAPH_AXIS_FONT),
                     opts->y_min_label);
        }
    }

    if (has_x_labels) {
        setFont(getFontPointer(GRAPH_AXIS_FONT));
        setColor(ui_fore[0], ui_fore[1], ui_fore[2]);

        int16_t label_y = (int16_t)(plot.bottom + 2);
        int16_t char_w = (int16_t)(GRAPH_AXIS_FONT / 2);

        if (opts->x_left != NULL) {
            drawText((uint16_t)plot.left, (uint16_t)label_y, opts->x_left);
        }
        if (opts->x_mid != NULL) {
            int16_t w = (int16_t)(strlen(opts->x_mid) * char_w);
            int16_t x = (int16_t)(plot.left + (plot.right - plot.left) / 2 - w / 2);
            if (x < plot.left) {
                x = plot.left;
            }
            drawText((uint16_t)x, (uint16_t)label_y, opts->x_mid);
        }
        if (opts->x_right != NULL) {
            int16_t w = (int16_t)(strlen(opts->x_right) * char_w);
            int16_t x = (int16_t)(plot.right - w);
            if (x < plot.left) {
                x = plot.left;
            }
            drawText((uint16_t)x, (uint16_t)label_y, opts->x_right);
        }
    }

    flushBuffer();
    if (banded) {
        endBand();
    }
}


/*
 * printLineTransparent: prints text to a line without drawing background pixels
 */
void printLineTransparent(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // Enable transparent background mode
    setTransparent(true);

    // Calculate Y position based on font size and line number
    uint32_t posY = calculateLineY(lineNum, fontSize);

    // Draw the text
    printToScreen(text, posY, posX, fontSize);

    // Disable transparent background mode
    setTransparent(false);

    flushBuffer();
}


/*
 * printToScreenInverted: prints a certain INVERTED text value to a certain position on the screen
 */
void printToScreenInverted(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // set colors INVERTED
    setColor(ui_back[0], ui_back[1], ui_back[2]);
    setbgColor(ui_fore[0], ui_fore[1], ui_fore[2]);

    // draw text after calculating Y position
    uint32_t posY = calculateLineY(lineNum, fontSize);
    printToScreen(text, posY, posX, fontSize);

    // set colors back to normal
    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    setbgColor(ui_back[0], ui_back[1], ui_back[2]);


    flushBuffer();
}


/*
 * printLineWithInversion: prints text with selective character inversion
 */
void printLineWithInversion(const char * text, const uint32_t lineNum, const uint32_t posX,
                            font_size_t fontSize, int invertStart, int invertEnd)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    int textLen = strlen(text);

    // Validate inversion range
    if (invertStart < 0) invertStart = 0;
    if (invertEnd >= textLen) invertEnd = textLen - 1;
    if (invertStart > invertEnd) {
        // No inversion, just print normally
        printLine(text, lineNum, posX, fontSize);
        return;
    }

    // Calculate Y position
    uint32_t posY = calculateLineY(lineNum, fontSize);

    // Get font width (approximate - most monospace fonts have width ~= height/2)
    uint32_t charWidth = (uint32_t)fontSize / 2;

    // Set font
    setFont(getFontPointer(fontSize));

    // Create buffers for text segments
    char segment[64];
    uint32_t currentX = posX;

    // Print text before inversion (if any)
    if (invertStart > 0) {
        setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
        setbgColor(ui_back[0], ui_back[1], ui_back[2]);

        strncpy(segment, text, invertStart);
        segment[invertStart] = '\0';
        drawText(currentX, posY, segment);
        currentX += charWidth * invertStart;
    }

    // Print inverted text
    setColor(ui_back[0], ui_back[1], ui_back[2]);
    setbgColor(ui_fore[0], ui_fore[1], ui_fore[2]);

    int invertLen = invertEnd - invertStart + 1;
    strncpy(segment, text + invertStart, invertLen);
    segment[invertLen] = '\0';
    drawText(currentX, posY, segment);
    currentX += charWidth * invertLen;

    // Print text after inversion (if any)
    if (invertEnd < textLen - 1) {
        setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
        setbgColor(ui_back[0], ui_back[1], ui_back[2]);

        strcpy(segment, text + invertEnd + 1);
        drawText(currentX, posY, segment);
    }

    // Restore normal colors
    setColor(ui_fore[0], ui_fore[1], ui_fore[2]);
    setbgColor(ui_back[0], ui_back[1], ui_back[2]);

    flushBuffer();
}


void changeContrast(const uint8_t contrast)
{
    // ssd1306AdjustContrast(contrast);
}


/* Tracks runtime on/off state (distinct from display_status, which is
 * "did init_display() succeed"). Starts true — the panel is left in
 * sleep-out/active state after init_display(). Used by handle_ui_input()
 * (ui.c) so any button press wakes a sleeping display before button events
 * are otherwise acted on. */
static bool display_awake = true;

bool display_is_awake(void)
{
    return display_awake;
}

/* Step interval. 25 ms gives 40 steps across a 1 s fade — smooth against the
 * ~10 ms panel frame period — and bounds how long a cancelling button press
 * waits to be noticed. */
#define DISPLAY_FADE_STEP_MS 25

/*
 * display_fade_out: ramps the backlight from its current level to fully dark
 * over duration_ms, then leaves it there. The caller is expected to actually
 * sleep the panel afterwards; this only does the light.
 *
 * "Fade to black" here is a backlight ramp, not a per-pixel dissolve. A pixel
 * dissolve would need the framebuffer we deliberately do not have: a full
 * frame is 128*160*2 = 40,960 B against ~38 KB free, and re-sending it per
 * animation step is ~41 ms at 8 MHz, so ~24 fps flat out with the SPI bus
 * saturated. The backlight is one PWM register and looks the same to the eye.
 *
 * The ramp is quadratic, not linear, because perceived brightness goes roughly
 * as luminance^(1/2.2) while PWM duty is roughly linear in luminance. A linear
 * duty ramp reads as "drops fast, then lingers near black". Squaring the
 * remaining fraction makes the *perceived* fall close to linear. The tail
 * lands on 0-1% for the last few steps, which is correct: those steps are
 * perceptually tiny, and 1% is the finest duty this API can express anyway.
 *
 * Touches only the backlight PWM, never SPI, so it deliberately does NOT take
 * display_draw_mutex — a 1 s fade must not lock out the drawing threads for a
 * second. It is safe to run concurrently with a redraw; the redraw just lands
 * on a dimmer screen.
 *
 * @param duration_ms  total fade time
 * @param cancelled    polled once per step; return true to abort. May be NULL
 *                     for an uninterruptible fade.
 * @return true if the fade completed (screen now dark), false if `cancelled`
 *         aborted it — in which case the backlight has already been put back
 *         to its remembered level and the caller should stay awake.
 */
bool display_fade_out(uint32_t duration_ms, bool (*cancelled)(void))
{
    uint32_t steps = duration_ms / DISPLAY_FADE_STEP_MS;
    uint32_t start = backlight_pct;

    /* Nothing to animate: already dark, or a duration too short to have even
     * one step. Report success — the caller's next move is to sleep anyway. */
    if (steps == 0 || start == 0) {
        ST7735S_backlightTransient(0);
        return true;
    }

    for (uint32_t i = 1; i <= steps; i++) {
        if (cancelled != NULL && cancelled()) {
            ST7735S_backlightRestore();
            return false;
        }

        uint32_t remaining = steps - i;
        ST7735S_backlightTransient(
            (uint8_t)((start * remaining * remaining) / (steps * steps)));

        k_msleep(DISPLAY_FADE_STEP_MS);
    }

    return true;
}


void switch_display(const bool on)
{
    if (on) {
        ST7735S_sleepOut();
    }
    else {
        ST7735S_sleepIn();
    }
    display_awake = on;
}






