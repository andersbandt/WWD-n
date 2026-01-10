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


// Display driver selection
// Uncomment the line below to use ST7735S instead of ST7789
#define USE_ST7735S


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

// line definitions
#ifdef USE_ST7735S
    #define line1_Y 35
    #define line2_Y 55
    #define line3_Y 95
    #define line4_Y 125
#else
    #define line1_Y 15
    #define line2_Y 45
    #define line3_Y 75
    #define line4_Y 105
#endif


/* Display driver selection */
#ifdef USE_ST7735S
    #include <st7735s.h>
    #include <gfx.h>
    #include <fonts.h>
#else
    #include <st7789.h>
#endif


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
 * @brief Handles printing text to whatever display is connected
 *
 * @param text: pointer to char for the string. String should terminate in \0
 *
 * @param posX: the x position to start printing the text to the screen
 *
 * @param posY: the y position to start printing the text to the screen
 */
void printToScreenInverted(char * text, int posX, int posY);


void changeContrast(const uint8_t contrast);


void switch_display(const bool on);



#endif /* SRC_HARDWARE_DISPLAY_H_ */
