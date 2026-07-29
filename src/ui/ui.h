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
#include <peripheral/clock.h>


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


/**
 * @brief Clock display data structure
 *
 * Contains all data displayed in UI_MODE_CLOCK with dirty flag tracking
 * for efficient updates.
 */
typedef struct {
    Time time;
    float imu_temp;
    int charging_status;
    int bat_percent;
    uint32_t step_count;
    uint32_t dirty_flags;  // Bitmask for tracking which fields changed
} ui_clock_data_t;

// Dirty flag bits
#define UI_CLOCK_DIRTY_TIME         (1 << 0)
#define UI_CLOCK_DIRTY_TEMP         (1 << 1)
#define UI_CLOCK_DIRTY_CHARGING     (1 << 2)
#define UI_CLOCK_DIRTY_BATTERY      (1 << 3)
#define UI_CLOCK_DIRTY_STEPS        (1 << 4)
#define UI_CLOCK_DIRTY_ALL          0xFFFFFFFF






extern ui_mode_t ui_mode;


/**
 * @brief Initializes the UI by displaying the main menu content on display
 */
void init_ui();


void ui_refresh();


void handle_ui_input();


void change_ui_mode(ui_mode_t new_mode);


void ui_fault(int code);


/**
 * @brief Mark clock data fields as dirty (needing update)
 * @param flags Bitmask of UI_CLOCK_DIRTY_* flags
 */
void ui_clock_mark_dirty(uint32_t flags);

/**
 * @brief Clear dirty flags for clock data fields
 * @param flags Bitmask of UI_CLOCK_DIRTY_* flags
 */
void ui_clock_clear_dirty(uint32_t flags);

/**
 * @brief Check if clock data fields are dirty
 * @param flags Bitmask of UI_CLOCK_DIRTY_* flags
 * @return true if any specified flag is dirty
 */
bool ui_clock_is_dirty(uint32_t flags);

/**
 * @brief Set clock time and mark dirty
 * @param time New time value
 */
void ui_clock_set_time(Time time);

/**
 * @brief Set IMU temperature and mark dirty
 * @param temp Temperature value
 */
void ui_clock_set_temp(float temp);

/**
 * @brief Set battery percentage and mark dirty
 * @param percent Battery percentage (0-100)
 */
void ui_clock_set_battery(int percent);

/**
 * @brief Set charging status and mark dirty
 * @param status Charging status value
 */
void ui_clock_set_charging(int status);

/**
 * @brief Set step count and mark dirty
 * @param steps Step count value
 */
void ui_clock_set_steps(uint32_t steps);


#endif /* SRC_UI_USERINTERFACE_H_ */

