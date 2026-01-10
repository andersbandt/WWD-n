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
#include <string.h>
#include <math.h>
#include <time.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>


/* My header files */
#include <display.h>


LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);



#define FORE_R 10
#define FORE_G 10
#define FORE_B 10

#define BACK_R 120
#define BACK_G 60
#define BACK_B 20

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
static uint8_t* getFontPointer(font_size_t fontSize)
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
#ifdef USE_ST7735S
    /* ST7735S initialization */
    ST7735S_Init();
    // setOrientation(R90);

    // set initial background
    setColor(FORE_R, FORE_G, FORE_B);
    setbgColor(BACK_R, BACK_G, BACK_B);
    fillScreen();

    // set display status
    display_status = 1;
#else
    /* ST7789 initialization (existing code) */
    ST7789_Init(ST77XX_ROTATE_270 | ST77XX_RGB);

    uint16_t i;
    ST7789_ClearScreen(WHITE);
    for (i=0; i<Screen.height; i=i+5) {
        ST7789_DrawLine(0, Screen.width, 0, i, RED);
    }
    for (i=0; i<Screen.height; i=i+5) {
        ST7789_DrawLine(0, Screen.width, i, 0, BLUE);
    }
    for (i=0; i<30; i++) {
        ST7789_FastLineHorizontal(0, Screen.width, i, BLACK);
    }
    ST7789_SetPosition(75, 5);
    ST7789_DrawString("ST7789V2 DRIVER", WHITE, X3);

    display_status = 1;
#endif
}



/**
 * clearDisplay: clears all content on the display
 */
void clear_display()
{
    setColor(FORE_R, FORE_G, FORE_B);
    setColor(BACK_R, BACK_G, BACK_B);
    fillScreen();
    flushBuffer();
}



/*
 * calculateLineY: calculates Y position for a line based on font size
 */
static uint32_t calculateLineY(uint32_t lineNum, font_size_t fontSize)
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

    // Clear the area with background color
    setColor(BACK_R, BACK_G, BACK_B);
    filledRect(posX - 2, posY - 2, 127, posY + fontHeight + 2);

    // Draw the text in white
    setColor(FORE_R, FORE_G, FORE_B);
    printToScreen(text, posY, posX, fontSize);

    flushBuffer();
}


/*
 * printLineTransparent: prints text to a line without drawing background pixels
 */
void printLineTransparent(const char * text, const uint32_t lineNum, const uint32_t posX, font_size_t fontSize)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // Calculate Y position based on font size and line number
    uint32_t posY = calculateLineY(lineNum, fontSize);

    // Enable transparent background mode
    setTransparent(true);

    // Draw the text
    printToScreen(text, posY, posX, fontSize);

    // Disable transparent background mode
    setTransparent(false);

    flushBuffer();
}


void printNum(int number, const uint32_t lineNum, const uint32_t posX)
{
    // TODO: finish this function to print a number
    // basically can relegate many sprintf calls to same function

    // HOWEVER also consider that I have `display_out_measurement` to work with
}


/*
 * printToScreenInverted: prints a certain INVERTED text value to a certain position on the screen
 */
// TODO: somehow have to combine this with my printLineTransparent
void printToScreenInverted(char * text, int posY, int posX)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // ssd1306PrintStringInverted(text, posY, posX, source_pro_set);
}



void changeContrast(const uint8_t contrast)
{
    // ssd1306AdjustContrast(contrast);
}


void switch_display(const bool on)
{
    if (on) {
        ST7735S_sleepOut();
    }
    else {
        ST7735S_sleepIn();
    }
}






