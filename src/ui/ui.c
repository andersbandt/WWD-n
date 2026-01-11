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

ui_mode_t ui_mode = UI_MODE_CLOCK; // internal variable
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
    LOG_INF("Initializing UI ...");
    ui_mode = UI_MODE_CLOCK;
    initMenu();
}


bool first_time = true;
void ui_refresh() {
    switch (ui_mode) {
        case UI_MODE_CLOCK:
            // Handle updating clock display
            Time cur_time = get_current_time();
            display_out_time(cur_time);

            if (first_time) {
                // handle updating battery percent
                // display_out_bms(charging_status, bat_percent);

                // handle updating health statistics
                // display_out_pedometer(step_count);

                // handle updating IMU temp
                // int16_t imu_temp = imu_get_temp(NULL);
                // display_out_temp(imu_temp);

                // TODO: delete this later
                first_time = false;
            }
            break;

        case UI_MODE_MENU:
            // Handle menu updates
            updateMenuScreen(0); // passing in NULL for `action`
            break;

        // System Settings UI Functions (Menu 0)
        case UI_MODE_PROMPT_TIME:
            system_prompt_for_time_UI_FUNC();
            break;

        case UI_MODE_CHANGE_CONTRAST:
            system_change_display_contrast_UI_FUNC();
            break;

        case UI_MODE_CLEAR_FAULTS:
            system_clear_faults_UI_FUNC();
            break;

        // IMU UI Functions (Menu 1)
        case UI_MODE_IMU_READ:
            imuRead_UI_FUNC();
            break;

        case UI_MODE_IMU_TEMP:
            imutempRead_UI_FUNC();
            break;

        default:
            LOG_ERR("Unknown UI mode: %d", ui_mode);
            ui_mode = UI_MODE_CLOCK; // Fallback to clock mode
            break;
    }
}

void handle_ui_input() {
    // do some delay (mainly to handle case for both button press)
    // do other shit
    if (ui_mode == UI_MODE_MENU) {
        uint8_t button_status = button_poll();

        // parse `button_status` into a format needed for UI menu API
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


void change_ui_mode(ui_mode_t new_mode) {
    // conduct some checks on if we want to change UI mode
    if (ui_mode == new_mode) {
        return;
    }
    else if (new_mode == UI_MODE_CLOCK) {
        if (get_running_state()) {
            return;
        }
    }

    ui_mode = new_mode;
    // update screen based on new mode
    if (ui_mode == UI_MODE_CLOCK) {
        initMenu();
        clear_display();
        ui_refresh();
    }
    else if (ui_mode == UI_MODE_MENU) {
        updateMainMenuScreen(0, 1); // TODO: calling this assumes that absolute_position = 0. How to ensure this?
    }
}


void ui_fault(int code) {
    display_out_fault(code);
    sleep(3);
    clear_display();
    ui_refresh();
}









