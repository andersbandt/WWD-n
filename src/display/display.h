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
void clearDisplay();


/**
 * @brief Handles printing text to whatever display is connected
 *
 * @param text: pointer to char for the string. String should terminate in \0
 *
 * @param posY: the line number to start printing the text to the screen
 *
 * @param posX: the x position to start printing the text to the screen (column location)
 */
void printToScreen(char * text, const uint32_t posY, const uint32_t posX);


/**
 * @brief
 *
 * @param text: pointer to char for string
 *
 * @param lineNum: line number to print to
 *
 * @param posX: x position to start printing to
 */
void printLine(char * text, const uint32_t lineNum, const uint32_t posX);


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


void switchDisplay(const bool on);



#endif /* SRC_HARDWARE_DISPLAY_H_ */
