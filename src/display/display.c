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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** @var Screen definition */
extern struct S_SCREEN Screen;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL FUNCTIONS -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * printToScreen: prints a certain text value to a certain position on the screen
 */
void printToScreen(const char * text, const uint32_t posY, const uint32_t posX)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

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
    setColor(60, 20, 20);
    fillScreen();

    setFont(ter_u24b);
    printLine("Line 1", 1, 4);
    printLine("Line 2", 2, 4);
    printLine("Line 3", 3, 4);
    printLine("Line 4", 4, 4);
    // drawText(4, 15, "Line 1");

    flushBuffer();

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
#endif
}



/**
 * clearDisplay: clears all content on the display
 */
void clear_display()
{
    setColor(0,0, 0);
    fillScreen();
}



/*
 * printLine: prints an individual line to the screen
 */
// TODO: do I wanna refactor this argument list order?
void printLine(const char * text, const uint32_t lineNum, const uint32_t posX)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // go through lines and print out at pre-determined line Y coordinates
    if (lineNum == 1) {
        printToScreen(text, line1_Y, posX);
    }
    else if (lineNum == 2) {
        printToScreen(text, line2_Y, posX);
    }
    else if (lineNum == 3) {
        printToScreen(text, line3_Y, posX);
    }
    else if (lineNum == 4) {
        printToScreen(text, line4_Y, posX);
    }
}




void printNum(int number, const uint32_t lineNum, const uint32_t posX) {
    // TODO: finish this function to print a number
    // basically can relegate many sprintf calls to same function

    // HOWEVER also consider that I have `display_out_measurement` to work with
}


/*
 * printToScreenInverted: prints a certain INVERTED text value to a certain position on the screen
 */
void printToScreenInverted(char * text, int posY, int posX)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    // ssd1306PrintStringInverted(text, posY, posX, source_pro_set);
}



void changeContrast(const uint8_t contrast) {
    // ssd1306AdjustContrast(contrast);
}


void switch_display(const bool on) {
    if (on) {
        ST7735S_sleepOut();
    }
    else {
        ST7735S_sleepIn();
    }
}






