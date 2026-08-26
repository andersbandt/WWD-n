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
    UI_MODE_LOW_POWER,                  // Low-power mode toggle (see low_power.h)
    UI_MODE_BLE,                        // Bluetooth on/off toggle (see ble.h)

    // IMU modes (Menu 1)
    UI_MODE_IMU_READ,                   // IMU accelerometer reading display
    UI_MODE_IMU_TEMP,                   // IMU temperature display
    UI_MODE_IMU_PEDOMETER,               // Step count display
    UI_MODE_TEMP_GRAPH,                  // Temperature history graph

    // Data modes (Menu 2)
    UI_MODE_DATA_STATS,                 // NVS log stats (write offset, meta seq)

    // Timer modes (Menu 3)
    UI_MODE_STOPWATCH,                  // Stopwatch (start/pause/reset)

    // Activity modes (Menu 4)
    UI_MODE_ACTIVITY,                   // Activity session start/stop (see activity.h)

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
    float soc_temp;         // nRF52833 die temp, degrees F (see soc_temp.h)
    int ble_on;             // Bluetooth radio enabled, see ble_is_enabled()
    int worn;               // watch is on a wrist, see imu_is_worn()
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
#define UI_CLOCK_DIRTY_SOC_TEMP     (1 << 7)   /* nRF52833 die temperature badge */
#define UI_CLOCK_DIRTY_BLE          (1 << 8)   /* Bluetooth status badge */
#define UI_CLOCK_DIRTY_WEAR         (1 << 9)   /* wear (on-wrist) badge */
#define UI_CLOCK_DIRTY_ALL          0xFFFFFFFF






/* Display auto-off: the panel sleeps this long after the last button press.
 * Matched to the 9 s sensor-refresh cadence (timer1) so the screen stays lit
 * across at least one full temp/step-count update after any interaction. */
#define UI_DISPLAY_TIMEOUT_MS 9000

/* How long the screen takes to fade to black once the timeout fires. The
 * panel actually sleeps at UI_DISPLAY_TIMEOUT_MS + UI_DISPLAY_FADE_MS, i.e.
 * the fade is added after the timeout rather than run up to it, so the screen
 * stays fully readable for the whole timeout. Costs one extra second of panel
 * wake time (~2.1 mA) per timeout event. */
#define UI_DISPLAY_FADE_MS 1000


extern ui_mode_t ui_mode;


/**
 * @brief Initializes the UI by displaying the main menu content on display
 */
void init_ui();


void ui_refresh();


void handle_ui_input();


/**
 * @brief handle_ui_input() for a press that has already been observed.
 *
 * @param forced_mask BUTTON_*_MASK of the press to act on, or 0 to poll the
 *                    buttons as handle_ui_input() does.
 *
 * Needed by callers that spend time classifying a press before dispatching it
 * — by then a short press has been released and polling would return 0. See
 * the comment on the implementation.
 */
void handle_ui_input_latched(uint8_t forced_mask);


/**
 * @brief Wakes a sleeping display in response to a non-button wake source.
 *
 * Runs the same wake + full clock repaint sequence handle_ui_input() runs for
 * a button press, factored out so another trigger can share it — currently
 * the WOM raise-to-wake path in button_handler_thread_entry() (main.c). A
 * no-op (just takes/releases display_draw_mutex) if the display is already
 * awake. Must be called from a thread, not an ISR — it takes
 * display_draw_mutex and touches SPI1.
 */
void ui_wake_display_if_asleep(void);


/**
 * @brief Jumps straight to the activity screen from anywhere.
 *
 * Bound to a long press of SW2 (top-right) so starting or stopping a session
 * never costs a walk through the menu — the whole point of a session marker is
 * that it lands when the activity actually starts, and a four-press detour
 * makes the timestamp wrong.
 *
 * Clears any latched sub-menu run state first, since change_ui_mode() refuses
 * the transition otherwise (see ui_menu_force_exit()). Must be called from a
 * thread, not an ISR — it takes display_draw_mutex and touches SPI1.
 */
void ui_open_activity_screen(void);


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

/* True while the current UI mode is a value-editing screen (time/date setter,
 * brightness), i.e. one where holding a button should auto-repeat. */
bool ui_autorepeat_active(void);


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
 * @brief Set the nRF52833 die temperature and mark dirty
 * @param temp Temperature in degrees Fahrenheit
 */
void ui_clock_set_soc_temp(float temp);

/**
 * @brief Set the Bluetooth indicator state and mark dirty
 * @param on non-zero if the radio is enabled
 */
void ui_clock_set_ble(int on);

/**
 * @brief Set the wear (on-wrist) indicator state and mark dirty
 * @param worn non-zero if the watch is being worn (imu_is_worn())
 */
void ui_clock_set_worn(int worn);

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

