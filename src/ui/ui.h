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
#include <src/ui/ui_display.h>


extern volatile int ui_mode;


/**
 * @brief Initializes the UI by displaying the main menu content on display
 */
void init_ui();


void ui_refresh();


void handle_ui_input(Display_Handle display);


void change_ui_mode(int new_mode);


void ui_fault(int code);


#endif /* SRC_UI_USERINTERFACE_H_ */

