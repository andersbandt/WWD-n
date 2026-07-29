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
#include <stdint.h>


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
