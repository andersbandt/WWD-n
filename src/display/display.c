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

// C header files
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* My header files */
#include <src/comm/i2c.h>
#include <src/display.h>
#include <src/st7789.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL VARIABLES -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////






void init_display(Display_Handle display) {
    ssd1306Init(1, display);
}



/**
 * clearDisplay: clears all content on the display
 */
// TODO: refactor this from `clearDisplay` to `clear_display`
void clearDisplay()
{
    ssd1306ClearDisplay();  // clear the display
}


/*
 * printToScreen: prints a certain text value to a certain position on the screen
 */
void printToScreen(char * text, const uint32_t posY, const uint32_t posX)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    ssd1306PrintString(text, posY, posX, source_pro_set);
}


/*
 * printLine: prints an individual line to the screen
 */
void printLine(char * text, const uint32_t lineNum, const uint32_t posX)
{
    if (text == 0) {  // handle null pointers being passed in
        return;
    }

    ssd1306PrintString(text, lineNum, posX, source_pro_set);
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

    ssd1306PrintStringInverted(text, posY, posX, source_pro_set);
}



void changeContrast(const uint8_t contrast) {
    ssd1306AdjustContrast(contrast);
}


void switchDisplay(const bool on) {
    ssd1306SwitchDisplay(on);
}






