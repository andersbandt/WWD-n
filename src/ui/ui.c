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
#include <stdio.h>
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
#include <peripheral/clock.h>
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

/* ui_refresh() (ui_refresh_thread, 1s tick) and handle_ui_input()
 * (button_handler_thread, on a real button interrupt) both call into the
 * same non-reentrant display/SPI1 drawing code (st7735s.c's window-tracking
 * globals, SPI transaction buffers) with no synchronization between them.
 * A button press landing mid-redraw corrupts that shared state and crashes
 * (observed live via GDB 2026-08-01: usage fault inside ui_refresh_thread,
 * RESETREAS=LOCKUP, reproduced after ruling out the nRF52 reset pin and
 * button-thread stack size). Same class of bug as the NAND/NVS concurrency
 * fix (mt29f_bus_mutex/nvs_state_mutex) — same fix shape here. */
K_MUTEX_DEFINE(display_draw_mutex);


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
    if (time.seconds % 15 == 0) {
        LOG_INF("ui_clock_set_time: %02d:%02d:%02d", time.hours, time.minutes, time.seconds);
    }
    clock_data.time = time;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_TIME);
}

/**
 * @brief Set IMU temperature and mark dirty
 */
void ui_clock_set_temp(float temp)
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
 * @brief Set battery voltage (raw divider reading, mV) and mark dirty
 */
void ui_clock_set_battery_mv(int mv)
{
    clock_data.bat_mv = mv;
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
 * @brief Set calendar date and mark dirty
 */
void ui_clock_set_date(Date date)
{
    clock_data.date = date;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_DATE);
}


/* Weekday + day-of-month header, top-left — replaces the old static "WWD-n"
 * title text (that slot is otherwise idle screen space). Sits at line 0
 * (y=2..~22, FONT_LARGE) which doesn't overlap the battery badge (top-right,
 * x>=82) or the time display (starts at y=CLOCK_TIME_Y=46), so it's safe to
 * redraw any time the screen gets cleared. */
static void draw_clock_title(void)
{
    char header[12];
    snprintf(header, sizeof(header), "%s %u", get_day_of_week_str(clock_data.date), clock_data.date.day);
    printLine(header, 0, 10, FONT_LARGE);
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

    /* Wipe the display_phase() splash screen ("WWD-n" / "display bring-up",
     * main.c) before the clock face draws over it — otherwise whatever the
     * splash didn't happen to overwrite (e.g. its background fill) is left
     * as a visible remnant around the clock digits. */
    clear_display();
    draw_clock_title();

    ui_mode = UI_MODE_CLOCK;
    display_clock_time_reset();  /* force a full HH:MM:SS redraw first time */


    // TODO: I can somehow make my UI testing easier now by just altering this UI_mode ... I started some testing thing that probably is old now
    // NOTE: I don't think it's currently working though because clicking buttons throws me into the menu ... might have to set ui_mode too?
    // ui_mode = UI_MODE_MENU;
    // ui_mode = UI_MODE_PROMPT_TIME;
    initMenu();
    ui_status = 1;
}


void ui_refresh() {
    k_mutex_lock(&display_draw_mutex, K_FOREVER);
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

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_BATTERY)) {
                display_out_battery(clock_data.bat_mv);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_BATTERY);
            }

            // enable UI_CLOCK_DIRTY_CHARGING/display_out_bms() when BMS is ready
            // (no charge-status GPIO wired yet, see power.c battery_charging())

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_STEPS)) {
                display_out_pedometer(clock_data.step_count);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_STEPS);
            }

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_DATE)) {
                draw_clock_title();
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_DATE);
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

        case UI_MODE_IMU_PEDOMETER:
            pedometer_UI_FUNC();
            break;

        case UI_MODE_TEMP_GRAPH:
            tempGraph_UI_FUNC();
            break;

        case UI_MODE_DATA_STATS:
            data_stats_UI_FUNC();
            break;

        case UI_MODE_STOPWATCH:
            stopwatch_UI_FUNC();
            break;

        default:
            LOG_ERR("Unknown UI mode: %d", ui_mode);
            ui_mode = UI_MODE_CLOCK; // Fallback to clock mode
            break;
    }
    k_mutex_unlock(&display_draw_mutex);
}


/* Physical button layout (SW1-4, clockwise from top-left) — see button.h:
 *   BUTTON_1_MASK = SW1 = top-left     = UP
 *   BUTTON_2_MASK = SW2 = top-right    = open/unassigned
 *   BUTTON_3_MASK = SW3 = bottom-right = SELECT
 *   BUTTON_4_MASK = SW4 = bottom-left  = DOWN
 * SW3+SW4 (the bottom row) together always return to the clock face,
 * regardless of UI mode. */
void handle_ui_input() {
    uint8_t button_status = button_poll();

    if (button_status == 0) {
        return;  /* nothing pressed */
    }

    /* Serialize against ui_refresh_thread's periodic redraw (ui_refresh(),
     * called every 1s) — both threads call into the same non-reentrant
     * display/SPI1 drawing code below, and a press landing mid-redraw
     * corrupted shared state badly enough to crash. See display_draw_mutex
     * comment near ui_mode's declaration. */
    k_mutex_lock(&display_draw_mutex, K_FOREVER);

    /* Any button press wakes a sleeping display first; that press just
     * wakes it and is not also treated as navigation (so waking up doesn't,
     * say, also jump a menu position or fire SELECT). Display timeout/sleep
     * itself isn't implemented yet (display_timeout_thread is disabled —
     * see main.c), so display_is_awake() is always true today; this is
     * scaffolding for when that lands. */
    if (!display_is_awake()) {
        switch_display(true);
        k_mutex_unlock(&display_draw_mutex);
        return;
    }

    /* Bottom row together = always home, regardless of mode. Clear any
     * latched sub-menu running state first or change_ui_mode() will refuse
     * the transition (see ui_menu_force_exit()). */
    if ((button_status & (BUTTON_3_MASK | BUTTON_4_MASK)) ==
        (BUTTON_3_MASK | BUTTON_4_MASK)) {
        ui_menu_force_exit();
        change_ui_mode(UI_MODE_CLOCK);
        k_mutex_unlock(&display_draw_mutex);
        return;
    }

    // Handle menu-specific input
    if (ui_mode == UI_MODE_MENU) {
        if (button_status == BUTTON_1_MASK) {        // SW1 top-left: UP
            updateMenuScreen(-1);
        }
        else if (button_status == BUTTON_4_MASK) {   // SW4 bottom-left: DOWN
            updateMenuScreen(1);
        }
        else if (button_status == BUTTON_3_MASK) {   // SW3 bottom-right: SELECT
            updateMenuScreen(2);
        }
        else if (button_status == BUTTON_2_MASK) {   // SW2 top-right: open/unassigned
            // TODO: no action defined yet for this button
        }
        k_mutex_unlock(&display_draw_mutex);
        return;
    }
    else if (ui_mode == UI_MODE_CLOCK) {
        // Any single button from the clock face opens the menu (the bottom-
        // row home combo above already returned before reaching here).
        change_ui_mode(UI_MODE_MENU);
    }

    k_mutex_unlock(&display_draw_mutex);

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
        draw_clock_title();
        display_clock_time_reset();  /* screen was just cleared — full redraw next time */
        ui_clock_mark_dirty(UI_CLOCK_DIRTY_ALL);  /* force temp/step badges to redraw too */
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


/* Remembers whatever was on screen before ui_show_dump_in_progress(true) so
 * the false call has something to restore beyond just "the clock face" —
 * see the caveat below. */
static ui_mode_t dump_prev_mode = UI_MODE_CLOCK;

/**
 * ui_show_dump_in_progress: overlay/restore the "FLASH DUMP IN PROGRESS"
 * message used by the USB host command protocol (protocol.c) during a
 * CMD_DUMP_START. A full NVS log dump runs silently for however long
 * app_pause_background_threads() has ui_refresh_thread/button_handler_thread
 * parked (large log ~= a while over CDC ACM), so without this the screen
 * just freezes on whatever it last showed with no indication a dump is
 * happening.
 *
 * Caller contract: only call this between app_pause_background_threads()
 * and app_resume_background_threads() (main.c) — with those threads parked,
 * this function (running on the protocol thread) has exclusive access to
 * display_draw_mutex/SPI1, same as the rest of handle_dump_start().
 *
 * Caveat: restoring (active=false) only fully repaints UI_MODE_CLOCK — the
 * common resting state. Other modes (menu navigation, a running stopwatch,
 * IMU screens) are best-effort via ui_refresh(): their _UI_FUNC()s use
 * static last-drawn-value/dirty-flag state that doesn't know the screen was
 * just wiped out from under it, so some fields may not repaint until they
 * next change. A dump landing mid-navigation is an edge case; revisit if it
 * turns out to matter in practice.
 *
 * @param active true to show the message, false to restore the prior screen
 */
void ui_show_dump_in_progress(bool active)
{
    if (active) {
        dump_prev_mode = ui_mode;

        k_mutex_lock(&display_draw_mutex, K_FOREVER);
        clear_display();
        printLine("FLASH DUMP", 1, 10, FONT_MEDIUM);
        printLine("IN PROGRESS", 2, 10, FONT_MEDIUM);
        k_mutex_unlock(&display_draw_mutex);
        return;
    }

    k_mutex_lock(&display_draw_mutex, K_FOREVER);
    clear_display();
    if (dump_prev_mode == UI_MODE_CLOCK) {
        draw_clock_title();
        display_clock_time_reset();  /* screen was just cleared — full redraw next time */
        ui_clock_mark_dirty(UI_CLOCK_DIRTY_ALL);
    }
    k_mutex_unlock(&display_draw_mutex);

    ui_refresh();  /* redraw whatever ui_mode is right now, immediately rather
                     * than waiting up to 1s for the next ui_refresh_thread tick */
}









