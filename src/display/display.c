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


/* Darcula palette, raw RGB565 fields (r:5 g:6 b:5) — see color-hex.com/color-palette/93326 */
#define BACK_R 5    // background   #2B2B2B
#define BACK_G 10
#define BACK_B 5

#define FORE_R 21   // primary text #A9B7C6
#define FORE_G 45
#define FORE_B 24

#define ACCENT_R 25 // accent (selection/highlight) #CC7832
#define ACCENT_G 30
#define ACCENT_B 6

#define DIM_R 11    // dim/secondary text #5C6773
#define DIM_G 25
#define DIM_B 14

/* Off-palette on purpose. The Darcula set above has no green and no red, and
 * the wear indicator is the one badge whose meaning is carried by hue rather
 * than by a label — a check and a cross are only legible as "good" and "bad"
 * if they are actually green and red. Kept adjacent to the palette so it is
 * obvious these two are the exception, not a second theme.
 *
 * *** These are TRUE red/green/blue, and printWearField() passes them to
 * setColor() with red and blue SWAPPED. That is not a typo. ***
 *
 * color565_t (st7735s.h) declares its bitfields `r:5, g:6, b:5`. On a
 * little-endian target the first-declared field takes the LOW bits, so `.r`
 * ends up in bits 0..4 — and after setColorC()'s byte swap in gfx.c that
 * reaches the panel where RGB565 expects BLUE. The struct is therefore BGR in
 * effect: the field named `r` is displayed as blue, and `b` as red.
 *
 * Every constant in the Darcula block above is written in struct order and so
 * inherits the swap — which is why ACCENT, documented as orange #CC7832,
 * actually renders blue on the panel. Correcting that is a whole-UI change and
 * is deliberately NOT done here; this badge just declares its colours honestly
 * and swaps at the one call site, where the swap is visible.
 *
 * Found the hard way: the cross shipped blue. Green hid it — 11/47/11 is
 * symmetric in red and blue, so the check looked correct either way. */
#define GOOD_RED 11   // wear: on-wrist  #58BC58
#define GOOD_GRN 47
#define GOOD_BLU 11

#define BAD_RED  28   // wear: off-wrist #E05450
#define BAD_GRN  21
#define BAD_BLU  10

/* Menu background — #F52091, as asked for.
 *
 * Written as TRUE red/green/blue; the swap into the panel's field order
 * happens in one place, in ui_back[] below. See the GOOD_/BAD_ note above for
 * why a swap is needed at all. */
#define MENU_BACK_RED 30   /* 0xF5 >> 3 */
#define MENU_BACK_GRN  8   /* 0x20 >> 2 */
#define MENU_BACK_BLU 18   /* 0x91 >> 3 */

/* The CURRENT background, in setColor()/setbgColor() ARGUMENT order — which on
 * this panel is blue-first.
 *
 * Everything that erases before drawing paints with this rather than with
 * BACK_* directly. That is the whole point: it is not enough to fill the
 * screen once on entering a mode, because every printLine()/printField*()/
 * printStatusField() clears its own box first, and each of those would punch a
 * dark-grey rectangle through a pink screen the moment it redrew.
 *
 * The Darcula grey is unaffected by the field swap (its red and blue are both
 * 5), which is why the initialiser can use BACK_* in argument order without
 * looking wrong. */
static uint8_t ui_back[3] = { BACK_R, BACK_G, BACK_B };

void display_set_menu_background(void)
{
    ui_back[0] = MENU_BACK_BLU;   /* first argument lands in the panel's blue */
    ui_back[1] = MENU_BACK_GRN;
    ui_back[2] = MENU_BACK_RED;
}

void display_set_default_background(void)
{
    ui_back[0] = BACK_R;
    ui_back[1] = BACK_G;
    ui_back[2] = BACK_B;
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

    setColor(FORE_R, FORE_G, FORE_B);
    setbgColor(ui_back[0], ui_back[1], ui_back[2]);
    // fillScreen();

    display_status = 1;
}



/**
 * clearDisplay: clears all content on the display
 */
void clear_display()
{
    setbgColor(ui_back[0], ui_back[1], ui_back[2]);
    fillScreen();
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
    setColor(FORE_R, FORE_G, FORE_B);
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

    setColor(FORE_R, FORE_G, FORE_B);
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

    setColor(FORE_R, FORE_G, FORE_B);
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

    /* Red and blue swapped on purpose — see the GOOD_ / BAD_ block above.
     * setColor()'s first argument lands in the panel's blue channel. */
    setColor(worn ? GOOD_BLU : BAD_BLU,
             worn ? GOOD_GRN : BAD_GRN,
             worn ? GOOD_RED : BAD_RED);

    wear_glyph_strokes(x0, posY, side, worn, drawLine);

    setColor(FORE_R, FORE_G, FORE_B);
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

    setColor(FORE_R, FORE_G, FORE_B);
    setFont(getFontPointer(fontSize));
    drawText(fieldLeft, posY, text);

    flushBuffer();
    if (banded) {
        endBand();
    }
}


/*
 * drawGraph: primitive line graph over a fixed pixel box - connects num_data
 * int16_t samples as a polyline scaled into [y_min, y_max], clamping any
 * out-of-range sample to that range so one spike can't blow out the whole
 * scale. Draws its own background + border, then flushes once at the end
 * (not once per segment), so an N-sample graph costs one SPI transfer
 * regardless of N.
 *
 * Deliberately reuses drawLine()/filledRect()/drawRect() from gfx.c rather
 * than adding new drawing primitives - drawLine is already linked into the
 * image via filledRect() (used throughout the existing UI: clearAndPrintLine,
 * printFieldRightAligned, etc.), so this function's marginal flash cost is
 * just its own scaling/loop logic, not a new copy of line-drawing code.
 *
 * @param data: sample array, left to right
 * @param num_data: sample count, must be >= 2
 * @param y_min, y_max: fixed value range to scale against (not auto-ranged
 *        from the data - caller decides the scale, e.g. a known sensor range)
 * @param left, top, right, bottom: pixel box to draw into, border included
 */
void drawGraph(const int16_t *data, size_t num_data, int16_t y_min, int16_t y_max,
               uint16_t left, uint16_t top, uint16_t right, uint16_t bottom)
{
    if (data == NULL || num_data < 2 || y_max <= y_min || right <= left || bottom <= top) {
        return;
    }

    uint16_t plot_w = right - left;
    uint16_t plot_h = bottom - top;
    int32_t y_range = (int32_t)y_max - (int32_t)y_min;

    /* Usually taller than the band, in which case this just returns false and
     * the graph redraws the way it always has. Worth attempting anyway: a
     * short graph box composites for free, and this is the redraw where the
     * clear is most visible because the box is large. */
    bool banded = beginFieldBand(left, top, right, bottom);

    setColor(ui_back[0], ui_back[1], ui_back[2]);
    filledRect(left, top, right, bottom);

    setColor(FORE_R, FORE_G, FORE_B);
    drawRect(left, top, right, bottom);

    for (size_t i = 0; i + 1 < num_data; i++) {
        int16_t v0 = data[i];
        int16_t v1 = data[i + 1];
        v0 = (v0 < y_min) ? y_min : (v0 > y_max) ? y_max : v0;
        v1 = (v1 < y_min) ? y_min : (v1 > y_max) ? y_max : v1;

        uint16_t x0 = left + (uint32_t)i       * plot_w / (num_data - 1);
        uint16_t x1 = left + (uint32_t)(i + 1) * plot_w / (num_data - 1);
        uint16_t y0 = bottom - (uint32_t)(v0 - y_min) * plot_h / y_range;
        uint16_t y1 = bottom - (uint32_t)(v1 - y_min) * plot_h / y_range;

        drawLine(x0, y0, x1, y1);
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
    setbgColor(FORE_R, FORE_G, FORE_B);

    // draw text after calculating Y position
    uint32_t posY = calculateLineY(lineNum, fontSize);
    printToScreen(text, posY, posX, fontSize);

    // set colors back to normal
    setColor(FORE_R, FORE_G, FORE_B);
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
        setColor(FORE_R, FORE_G, FORE_B);
        setbgColor(ui_back[0], ui_back[1], ui_back[2]);

        strncpy(segment, text, invertStart);
        segment[invertStart] = '\0';
        drawText(currentX, posY, segment);
        currentX += charWidth * invertStart;
    }

    // Print inverted text
    setColor(ui_back[0], ui_back[1], ui_back[2]);
    setbgColor(FORE_R, FORE_G, FORE_B);

    int invertLen = invertEnd - invertStart + 1;
    strncpy(segment, text + invertStart, invertLen);
    segment[invertLen] = '\0';
    drawText(currentX, posY, segment);
    currentX += charWidth * invertLen;

    // Print text after inversion (if any)
    if (invertEnd < textLen - 1) {
        setColor(FORE_R, FORE_G, FORE_B);
        setbgColor(ui_back[0], ui_back[1], ui_back[2]);

        strcpy(segment, text + invertEnd + 1);
        drawText(currentX, posY, segment);
    }

    // Restore normal colors
    setColor(FORE_R, FORE_G, FORE_B);
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






