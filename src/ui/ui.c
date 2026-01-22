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

int ui_status = 0;
ui_mode_t ui_mode = UI_MODE_CLOCK; // internal variable
// volatile uint32_t step_count; // defined in imu.h


int bat_percent; // defined in BQ25120A.h
int charging_status; // defined in BQ25120A.h


// Static clock data with dirty tracking
static ui_clock_data_t clock_data = {
    .dirty_flags = UI_CLOCK_DIRTY_ALL  // Start with everything dirty
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Mark clock data fields as dirty (needing update)
 */
void ui_clock_mark_dirty(uint32_t flags)
{
    clock_data.dirty_flags |= flags;
}

/**
 * @brief Clear dirty flags for clock data fields
 */
void ui_clock_clear_dirty(uint32_t flags)
{
    clock_data.dirty_flags &= ~flags;
}

/**
 * @brief Check if clock data fields are dirty
 */
bool ui_clock_is_dirty(uint32_t flags)
{
    return (clock_data.dirty_flags & flags) != 0;
}

/**
 * @brief Set clock time and mark dirty
 */
void ui_clock_set_time(Time time)
{
    clock_data.time = time;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_TIME);
}

/**
 * @brief Set IMU temperature and mark dirty
 */
void ui_clock_set_temp(int16_t temp)
{
    clock_data.imu_temp = temp;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_TEMP);
}

/**
 * @brief Set battery percentage and mark dirty
 */
void ui_clock_set_battery(int percent)
{
    clock_data.bat_percent = percent;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_BATTERY);
}

/**
 * @brief Set charging status and mark dirty
 */
void ui_clock_set_charging(int status)
{
    clock_data.charging_status = status;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_CHARGING);
}

/**
 * @brief Set step count and mark dirty
 */
void ui_clock_set_steps(uint32_t steps)
{
    clock_data.step_count = steps;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_STEPS);
}


/**
 * initUI: initializes the user interface
 */
void init_ui()
{
    LOG_INF("Initializing UI ...");
    if (display_status == 0) {
        LOG_ERR("Can't initialize UI! Display has bad status");
        ui_status = 0;
        return;
    }

    ui_mode = UI_MODE_CLOCK;
    initMenu();
    ui_status = 1;
}


void ui_refresh() {
    switch (ui_mode) {
        case UI_MODE_CLOCK:
            // Handle updating clock display using dirty flags
            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_TIME)) {
                display_out_time(clock_data.time, TIME_INVERT_NONE);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_TIME);
            }

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_TEMP)) {
                display_out_temp(clock_data.imu_temp);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_TEMP);
            }

            // enable when BMS is ready
            // if (ui_clock_is_dirty(UI_CLOCK_DIRTY_CHARGING | UI_CLOCK_DIRTY_BATTERY)) {
            //     display_out_bms(clock_data.charging_status, clock_data.bat_percent);
            //     ui_clock_clear_dirty(UI_CLOCK_DIRTY_CHARGING | UI_CLOCK_DIRTY_BATTERY);
            // }

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_STEPS)) {
                display_out_pedometer(clock_data.step_count);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_STEPS);
            }
            break;

        case UI_MODE_MENU:
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
    uint8_t button_status = button_poll();

    // Handle menu-specific input
    if (ui_mode == UI_MODE_MENU) {
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
        return;
    }

    // Push non-zero button events to buffer for UI functions to consume
    if (button_status != 0) {
        button_buffer_push(button_status);
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
        abs_position = 0;
        updateMainMenuScreen(0, 1);
    }
}


void ui_fault(int code) {
    display_out_fault(code);
    sleep(3);
    clear_display();
    ui_refresh();
}









