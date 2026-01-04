//*****************************************************************************
//!
//! @file userInterface.c
//! @author Anders Bandt
//! @brief Provides user functionality through display
//! @version 1.0
//! @date January 2022
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* C99 header files */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

/* Driver header files */
#include <ti/display/Display.h>

/* UI specific header files */
#include "src/ui/ui.h"
#include "src/ui/ui_menu.h"
#include "src/ui/ui_display.h"
#include "src/ui/UIFunctions.h"
#include "ui_menu.h"

/* My header files */
#include <src/display.h>
#include <src/clock.h>
#include <src/ic/bms/BQ25120A.h>
#include <src/ic/imu/imu.h>
#include <src/hardware/button.h>
#include <ui/ui_menu.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

volatile int ui_mode; // defined in ui.h
volatile uint32_t step_count; // defined in imu.h


int bat_percent; // defined in BQ25120A.h
int charging_status; // defined in BQ25120A.h


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * initUI: initializes the user interface
 */
void init_ui(Display_Handle display)
{
    Display_printf(display, 0, 0,"______________________________\n");
    Display_printf(display, 0, 0,"\nInitializing the user interface\n");

    ui_mode = 1; // set initial ui_mode at 1 (clock)
    initMenu();

    /* if (ssd1306_init) { */ // TODO: finish this implementatinon of OLED init check
    if (1) {
        Display_printf(display, 0, 0,"User interface initialized\n");
    }

    // if ssd1306 has not been properly initialized
    else {
        Display_printf(display, 0, 0,"ERROR: can't start user interface: ssd1306 not initialized\n");
    }
}


void ui_refresh() {
    if (ui_mode == 1) {
        // handle updating clock
        Time cur_time = get_current_time();
        display_out_time(cur_time);

        // handle updating battery percent
        display_out_bms(charging_status, bat_percent);

        // handle updating health statistics
        display_out_pedometer(step_count);


        // handle updating IMU temp
        int16_t imu_temp = imu_get_temp(NULL);
        display_out_temp(imu_temp);
        
    }
    else if (ui_mode == 2) {
        // handle menu updates
        updateMenuScreen(0); // passing in NULL for `action`
    }

}

void handle_ui_input(Display_Handle display) {
    // do some delay (mainly to handle case for both button press)
    usleep(100*480);

    // do other shit
    if (ui_mode == 2) {
        uint8_t button_status = button_poll();
        /* Display_printf(display, 0, 0, "\tui input: [%u]", button_status); */

        // parse `button_status` into a format needed for UI menu APIy
        if (button_status == 1) {
            updateMenuScreen(1);
        }
        else if (button_status == 2) {
            updateMenuScreen(-1);
        }
        else if (button_status == 0) {
            updateMenuScreen(2); // BOTH button (select)
        }
    }
}


void change_ui_mode(int new_mode) {
    // conduct some checks on if we want to change UI mode
    if (ui_mode == new_mode) {
        return;
    }
    else if (new_mode == 1) {
        if (get_running_state()) {
            return;
        }
    }

    ui_mode = new_mode;
    // update screen based on new mode
    if (ui_mode == 1) {
        initMenu();
        clearDisplay();
        ui_refresh();
    }
    else if (ui_mode == 2) {
        updateMainMenuScreen(0, 1); // TODO: calling this assumes that absolute_position = 0. How to ensure this?
    }
}


void ui_fault(int code) {
    display_out_fault(code);
    sleep(3);
    clearDisplay();
    ui_refresh();
}









