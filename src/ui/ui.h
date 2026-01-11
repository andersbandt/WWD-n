//*****************************************************************************
//!
//! @file userInterface.h
//! @author Anders Bandt
//! @brief Header file for user interface information
//! @version 1.0
//! @date January 2022
//!
//*****************************************************************************


#ifndef SRC_UI_USERINTERFACE_H_
#define SRC_UI_USERINTERFACE_H_


#include <stdint.h>
#include <ui_display.h>


/**
 * @brief UI mode enumeration
 *
 * Defines all possible UI modes/screens. Each mode has specific behavior
 * in ui_refresh() and handle_ui_input().
 */
typedef enum {
    UI_MODE_CLOCK = 1,                  // Clock display mode (default)
    UI_MODE_MENU = 2,                   // Menu navigation mode

    // System Settings modes (Menu 0)
    UI_MODE_PROMPT_TIME,                // Time setting interface
    UI_MODE_CHANGE_CONTRAST,            // Display contrast adjustment
    UI_MODE_CLEAR_FAULTS,               // Fault clearing interface

    // IMU modes (Menu 1)
    UI_MODE_IMU_READ,                   // IMU accelerometer reading display
    UI_MODE_IMU_TEMP,                   // IMU temperature display

    // Add more modes as needed for future UI functions
} ui_mode_t;

extern ui_mode_t ui_mode;


/**
 * @brief Initializes the UI by displaying the main menu content on display
 */
void init_ui();


void ui_refresh();


void handle_ui_input();


void change_ui_mode(ui_mode_t new_mode);


void ui_fault(int code);


#endif /* SRC_UI_USERINTERFACE_H_ */

