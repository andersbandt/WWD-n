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

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* UI specific header files */
#include <ui.h>
#include <ui_menu.h>
#include <ui_display.h>
#include <UIFunctions.h>
#include <ui_menu.h>

/* My header files */
#include <display.h>
#include <clock.h>
#include <imu.h>
#include <hardware/button.h>
#include <hardware/led.h>
#include <ui_menu.h>


LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ui_mode = 0; // internal variable
// volatile uint32_t step_count; // defined in imu.h


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
void init_ui()
{
    ui_mode = 1;
    initMenu();
}


bool first_time = true;
void ui_refresh() {
    if (ui_mode == 1) {
        // handle updating clock
        Time cur_time = get_current_time();
        display_out_time(cur_time);

        if (first_time) {
            // handle updating battery percent
            // display_out_bms(charging_status, bat_percent);
            // display_out_bms(1, 99);

            // handle updating health statistics
            // display_out_pedometer(step_count);
            // display_out_pedometer(4123);


            // handle updating IMU temp
            // int16_t imu_temp = imu_get_temp(NULL);
            // display_out_temp(imu_temp);
            // display_out_temp(70);

            // TODO: delete this later
            first_time = false;
        }
        
    }
    else if (ui_mode == 2) {
        // handle menu updates
        updateMenuScreen(0); // passing in NULL for `action`
    }

}

void handle_ui_input() {
    // do some delay (mainly to handle case for both button press)
    // do other shit
    if (ui_mode == 2) {
        uint8_t button_status = button_poll();

        // parse `button_status` into a format needed for UI menu APIy
        if (button_status == 1) {
            updateMenuScreen(-1);
        }
        else if (button_status == 2) {
            updateMenuScreen(1);
        }
        else if (button_status == 4) {
            updateMenuScreen(2);
        }
        else if (button_status == 8) {
            updateMenuScreen(2);
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
        clear_display();
        ui_refresh();
    }
    else if (ui_mode == 2) {
        updateMainMenuScreen(0, 1); // TODO: calling this assumes that absolute_position = 0. How to ensure this?
    }
}


void ui_fault(int code) {
    display_out_fault(code);
    sleep(3);
    clear_display();
    ui_refresh();
}









