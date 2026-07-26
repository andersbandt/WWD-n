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
    setbgColor(BACK_R, BACK_G, BACK_B);
    // fillScreen();

    display_status = 1;
}



/**
 * clearDisplay: clears all content on the display
 */
void clear_display()
{
    setbgColor(BACK_R, BACK_G, BACK_B);
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
    setColor(BACK_R, BACK_G, BACK_B);
    setbgColor(FORE_B, FORE_G, FORE_B);

    // draw text after calculating Y position
    uint32_t posY = calculateLineY(lineNum, fontSize);
    printToScreen(text, posY, posX, fontSize);

    // set colors back to normal
    setColor(FORE_B, FORE_G, FORE_B);
    setbgColor(BACK_R, BACK_G, BACK_B);


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
        setbgColor(BACK_R, BACK_G, BACK_B);

        strncpy(segment, text, invertStart);
        segment[invertStart] = '\0';
        drawText(currentX, posY, segment);
        currentX += charWidth * invertStart;
    }

    // Print inverted text
    setColor(BACK_R, BACK_G, BACK_B);
    setbgColor(FORE_R, FORE_G, FORE_B);

    int invertLen = invertEnd - invertStart + 1;
    strncpy(segment, text + invertStart, invertLen);
    segment[invertLen] = '\0';
    drawText(currentX, posY, segment);
    currentX += charWidth * invertLen;

    // Print text after inversion (if any)
    if (invertEnd < textLen - 1) {
        setColor(FORE_R, FORE_G, FORE_B);
        setbgColor(BACK_R, BACK_G, BACK_B);

        strcpy(segment, text + invertEnd + 1);
        drawText(currentX, posY, segment);
    }

    // Restore normal colors
    setColor(FORE_R, FORE_G, FORE_B);
    setbgColor(BACK_R, BACK_G, BACK_B);

    flushBuffer();
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






