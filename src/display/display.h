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


/**
 * @brief Draws a primitive line graph over a fixed pixel box
 *
 * Connects num_data samples as a polyline scaled into [y_min, y_max] (values
 * outside that range are clamped, not auto-ranged), with its own background
 * and border. Flushes once at the end regardless of sample count.
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
 * @brief Returns true if the display is currently awake (sleep-out/active).
 */
bool display_is_awake(void);



#endif /* SRC_HARDWARE_DISPLAY_H_ */
