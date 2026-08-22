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
    UI_MODE_ADJUST_BRIGHTNESS,          // Backlight brightness adjustment
    UI_MODE_CLEAR_FAULTS,               // Fault clearing interface

    // IMU modes (Menu 1)
    UI_MODE_IMU_READ,                   // IMU accelerometer reading display
    UI_MODE_IMU_TEMP,                   // IMU temperature display
    UI_MODE_IMU_PEDOMETER,               // Step count display
    UI_MODE_TEMP_GRAPH,                  // Temperature history graph

    // Data modes (Menu 2)
    UI_MODE_DATA_STATS,                 // NVS log stats (write offset, meta seq)

    // Timer modes (Menu 3)
    UI_MODE_STOPWATCH,                  // Stopwatch (start/pause/reset)

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
    Date date;
    float imu_temp;
    int charging_status;
    int low_power;          // TPS63900 power-save mode, see power_save_is_enabled()
    int bat_percent;
    int bat_mv;             // raw divider reading, mV (see battery_voltage_mv() in power.c)
    uint32_t step_count;
    uint32_t dirty_flags;  // Bitmask for tracking which fields changed
} ui_clock_data_t;

// Dirty flag bits
#define UI_CLOCK_DIRTY_TIME         (1 << 0)
#define UI_CLOCK_DIRTY_TEMP         (1 << 1)
#define UI_CLOCK_DIRTY_CHARGING     (1 << 2)
#define UI_CLOCK_DIRTY_BATTERY      (1 << 3)
#define UI_CLOCK_DIRTY_STEPS        (1 << 4)
#define UI_CLOCK_DIRTY_DATE         (1 << 5)
#define UI_CLOCK_DIRTY_POWER        (1 << 6)   /* charging + low-power indicators */
#define UI_CLOCK_DIRTY_ALL          0xFFFFFFFF






/* Display auto-off: the panel sleeps this long after the last button press.
 * Matched to the 9 s sensor-refresh cadence (timer1) so the screen stays lit
 * across at least one full temp/step-count update after any interaction. */
#define UI_DISPLAY_TIMEOUT_MS 9000


extern ui_mode_t ui_mode;


/**
 * @brief Initializes the UI by displaying the main menu content on display
 */
void init_ui();


void ui_refresh();


void handle_ui_input();


/**
 * @brief Records user activity, restarting the display auto-off countdown.
 *
 * Called on every real button press; also called by init_ui() so the screen
 * stays lit for one timeout period after boot.
 */
void ui_note_activity(void);


/**
 * @brief One-second tick for the display auto-off timeout.
 *
 * Puts the panel to sleep (backlight off + SLPIN, see switch_display()) once
 * UI_DISPLAY_TIMEOUT_MS has passed with no button press. Safe to call when
 * the display is already asleep or was never initialized. Must be called
 * from a thread, not an ISR — it takes display_draw_mutex and touches SPI1.
 */
void ui_idle_tick(void);


void change_ui_mode(ui_mode_t new_mode);


void ui_fault(int code);


/**
 * @brief Shows/hides the "FLASH DUMP IN PROGRESS" overlay used during CMD_DUMP_START
 *
 * Must only be called while background threads are parked — see the doc
 * comment in ui.c for the full contract.
 *
 * @param active true to show the message, false to restore the prior screen
 */
void ui_show_dump_in_progress(bool active);


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
 * @brief Set battery voltage (raw divider reading, mV) and mark dirty
 * @param mv Battery voltage in millivolts
 */
void ui_clock_set_battery_mv(int mv);

/**
 * @brief Set charging status and mark dirty
 * @param status Charging status value
 */
void ui_clock_set_charging(int status);

/**
 * @brief Set low-power (TPS63900 power-save) indicator state and mark dirty
 * @param low_power non-zero if power-save mode is active
 */
void ui_clock_set_low_power(int low_power);

/**
 * @brief Set step count and mark dirty
 * @param steps Step count value
 */
void ui_clock_set_steps(uint32_t steps);

/**
 * @brief Set calendar date and mark dirty
 * @param date New date value
 */
void ui_clock_set_date(Date date);


#endif /* SRC_UI_USERINTERFACE_H_ */

