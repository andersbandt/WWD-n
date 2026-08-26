//*****************************************************************************
//!
//! @file UIFunctions.c
//! @author Anders Bandt
//! @brief This file is for defining functions that get called from the user interface
//! @version 1.0
//! @date Feburary 2022
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Shares ui.c's log module rather than registering a second one — these are
 * the same subsystem, and the erase path below needs to leave a record that a
 * destructive action was taken from the device UI. */
LOG_MODULE_DECLARE(ui, LOG_LEVEL_INF);

/* My header files */
#include <hardware/button.h>
#include <imu.h>
#include <imu_bringup.h>   /* imu_alive — lets the empty-state screens say WHY */
#include <peripheral/clock.h>
#include <peripheral/rv3028.h>
#include <memory/nvs.h>
#include <power/low_power.h>

/* UI and display */
#include <display.h>
#include <ui.h>
#include <ui_menu.h>       /* ui_menu_return_to_sub_menu() for Cancel */
#include <activity/activity.h>
#include <ble/ble.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Time time_offset;
Date date_offset;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL VARIABLES -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int position = 0;  // field cursor within the current setting screen (see system_prompt_for_time_UI_FUNC)
bool first_ui_time = true; // useful for doing things the first time a function has to get called

/* Which screen of the time/date setter is showing. Reset with position by
 * reset_uifunc_params() so re-entering the setter always starts at TIME. */
typedef enum {
    SET_SCREEN_TIME = 0,   // HH:MM:SS
    SET_SCREEN_DATE = 1,   // MM/DD/YYYY
} set_screen_t;

static set_screen_t set_screen = SET_SCREEN_TIME;

/* Separate from stopwatch_running/stopwatch_elapsed_ms below - this only
 * tracks whether the screen itself needs a one-time full clear_display()
 * on entry, so it's safe to reset on every reset_uifunc_params() call
 * without disturbing the actual timer state. */
static bool stopwatch_screen_dirty = true;
/* Same contract as stopwatch_screen_dirty: set whenever the screen needs a
 * full repaint (entered fresh, or the cursor moved). */
static bool activity_screen_dirty = true;
static size_t activity_cursor;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void reset_uifunc_params() {
    position = 0;
    set_screen = SET_SCREEN_TIME;
    first_ui_time = true;
    stopwatch_screen_dirty = true;
    activity_screen_dirty = true;
    activity_cursor = 0;
}


// RESOLVED 2026-08-22: audited this file — no k_sleep/k_msleep/k_usleep and no blocking
// while loop left anywhere in it (get_button_event() is a plain non-blocking circular-buffer
// pop, not a wait). The one that existed (a `while (status)` + k_usleep(10000) loop in
// system_adjust_brightness_UI_FUNC(), see the comment there for what it actually broke) is
// already fixed — every _UI_FUNC() here is now a single non-blocking pass per ui_refresh()
// tick, same shape. Kept as a marker (rather than deleted) because two other comments in
// this file point back at it by name.

/////////////////////////////////////////////////////
////////// MENU 0 - SYSTEM SETTINGS /////////////////
/////////////////////////////////////////////////////

/*
 * system_prompt_for_time_UI_FUNC: two-screen time/date setter.
 *
 * Button assignment (physical layout, SW1-4 clockwise from top-left — see
 * button.h). The two axes of editing are split left/right rather than being
 * spread across all four corners: the left column changes the *value* under
 * the cursor, the right column moves the *cursor*.
 *
 *   SW1 (top-left)     INCREMENT the selected field
 *   SW4 (bottom-left)  DECREMENT the selected field
 *   SW2 (top-right)    NEXT SCREEN (TIME -> DATE -> commit & exit)
 *   SW3 (bottom-right) NEXT FIELD within the current screen (wraps)
 *
 * RESOLVED 2026-08-23: SW1/SW4 were backwards - SW1 (top-left) is UP
 * everywhere else in the UI (BUTTON_ACTION_UP, menu scroll) but decremented
 * here, so the "up" button counted down. Same swap applied to the brightness
 * screen below, which had the identical inversion.
 *
 * Screens:
 *   SET_SCREEN_TIME  "HH:MM:SS"    fields: hours -> minutes -> seconds
 *   SET_SCREEN_DATE  "MM/DD/YYYY"  fields: month -> day -> year
 *
 * Month/day/year live on one screen (previously one screen each), so the
 * whole date is visible while editing any part of it — which also matters for
 * day-of-month clamping, since increment_day() depends on the month/year
 * shown alongside it.
 *
 * RESOLVED 2026-08-22: both screens are now seeded from the real RV-3028
 * hardware RTC on entry (get_current_time()/get_date(), rather than starting
 * from a zeroed/stale in-memory value) and both are committed back to it on
 * exit (rv3028_set_time() directly; set_date() already goes through
 * rv3028_set_date()). Previously only the date half of this actually worked —
 * time_offset was edited on screen but never written anywhere, so every
 * "time set" was silently discarded. See imu_notes.md-style history in
 * CLAUDE.md Known Issues for the old state; that entry is now stale.
 */

#define SET_FIELDS_PER_SCREEN 3

/* Redraws the current screen in full: the title line plus the value line with
 * the selected field inverted. Both screens draw their value on the same line
 * via the same font (see display_out_date() in ui_display.c), so paging
 * between them never leaves a stale longer string behind — except the title,
 * which clearAndPrintLine() clears for us. */
static void draw_time_set_screen(void)
{
    if (set_screen == SET_SCREEN_TIME) {
        clearAndPrintLine("SET TIME", 0, 12, FONT_LARGE);
        display_out_time(time_offset,
                         position == 0 ? TIME_INVERT_HOURS :
                         position == 1 ? TIME_INVERT_MINUTES :
                                         TIME_INVERT_SECONDS);
    }
    else {
        clearAndPrintLine("SET DATE", 0, 12, FONT_LARGE);
        display_out_date(date_offset,
                         position == 0 ? DATE_INVERT_MONTH :
                         position == 1 ? DATE_INVERT_DAY :
                                         DATE_INVERT_YEAR);
    }
}

/* Applies dir to whichever field the cursor is on. */
static void adjust_selected_field(direction_t dir)
{
    if (set_screen == SET_SCREEN_TIME) {
        if (position == 0) {
            time_offset.hours = increment_hour(time_offset.hours, dir);
        }
        else if (position == 1) {
            time_offset.minutes = increment_minute(time_offset.minutes, dir);
        }
        else {
            time_offset.seconds = increment_second(time_offset.seconds, dir);
        }
        return;
    }

    if (position == 0) {
        date_offset.month = increment_month(date_offset.month, dir);
        /* A shorter month can strand the day out of range (e.g. Jan 31 -> Feb).
         * Clamp rather than letting an invalid date reach set_date(). */
        uint8_t max_day = days_in_month(date_offset.month, date_offset.year);
        if (date_offset.day > max_day) {
            date_offset.day = max_day;
        }
    }
    else if (position == 1) {
        date_offset.day = increment_day(date_offset.day, date_offset.month, date_offset.year, dir);
    }
    else {
        date_offset.year = increment_year(date_offset.year, dir);
        /* Same clamp as above, for Feb 29 in a year that stops being a leap year. */
        uint8_t max_day = days_in_month(date_offset.month, date_offset.year);
        if (date_offset.day > max_day) {
            date_offset.day = max_day;
        }
    }
}

void system_prompt_for_time_UI_FUNC() {
    if (first_ui_time) {
        date_offset = get_date();          // seed from the real (RTC-backed) date, not zeroed
        time_offset = get_current_time();  // seed from the real (RTC-backed) time, not zeroed
        set_screen = SET_SCREEN_TIME;
        position = 0;
        /* Wipe whatever the menu left on screen — the two lines this function
         * draws don't cover the whole panel on their own. */
        clear_display();
        draw_time_set_screen();
        button_buffer_clear();
        first_ui_time = false;
    }

    /* Drain every queued press, then redraw ONCE. Each redraw is a full
     * clearAndPrintLine() + display_out_time() over SPI, far slower than the
     * arithmetic in adjust_selected_field() - so a burst of presses used to
     * cost a burst of full repaints, and the screen crawled along behind the
     * user's thumb one step at a time. Coalescing means a spammed burst
     * lands as a single jump straight to the final value. */
    uint8_t btn_poll;
    bool redraw = false;

    while ((btn_poll = get_button_event()) != 0) {
        // INCREMENT (SW1, top-left — UP, same as everywhere else in the UI)
        if (btn_poll == BUTTON_1_MASK) {
            adjust_selected_field(DIR_UP);
            redraw = true;
        }
        // DECREMENT (SW4, bottom-left)
        else if (btn_poll == BUTTON_4_MASK) {
            adjust_selected_field(DIR_DOWN);
            redraw = true;
        }
        // NEXT FIELD within this screen (SW3, bottom-right)
        else if (btn_poll == BUTTON_3_MASK) {
            position = (position + 1) % SET_FIELDS_PER_SCREEN;
            redraw = true;
        }
        // NEXT SCREEN (SW2, top-right)
        else if (btn_poll == BUTTON_2_MASK) {
            /* Paging/committing ends this drain: anything still queued was
             * aimed at the screen we're leaving, so it must not be replayed
             * against the next one. button_buffer_clear() drops it and we
             * return rather than continuing the loop. */
            button_buffer_clear();

            if (set_screen == SET_SCREEN_TIME) {
                set_screen = SET_SCREEN_DATE;
                position = 0;
                draw_time_set_screen();
            }
            else {
                rv3028_set_time(time_offset);  // commit both to the RV-3028 (I2C write)
                set_date(date_offset);         // (set_date() -> rv3028_set_date(), same chip)
                ui_clock_set_date(date_offset);
                ui_mode = UI_MODE_CLOCK;
                /* Direct assignment bypasses change_ui_mode(), so the menu
                 * background and the leftover setter screen have to be cleared
                 * here or the clock face draws on pink over the old text. */
                ui_enter_clock_face();
            }
            return;
        }
    }

    if (redraw) {
        draw_time_set_screen();
    }

    return;
}



/*
 * system_adjust_brightness_UI_FUNC: backlight brightness screen.
 *
 *   SW1 (top-left)     INCREMENT brightness
 *   SW4 (bottom-left)  DECREMENT brightness
 *   SW3+SW4            exit to the clock face (the global home combo)
 *
 * Same left-column-changes-the-value convention as the time/date setter.
 *
 * This used to spin in its own `while (status)` loop with a k_usleep(10000),
 * which is why the value appeared to oscillate between 95 and 100 on its own:
 * the loop's exit condition was `btn_poll == 0`, and get_button_event()
 * returns 0 whenever the event buffer is *empty* - i.e. the loop fell out
 * almost immediately every time. `brightness` was a stack local re-initialised
 * to 100 on each entry, and ui_refresh() re-entered the function on every 1s
 * tick, so the only reachable states were "100" (fresh entry) and "95" (a
 * single decrement landing before the buffer drained). Nothing the user
 * pressed could accumulate.
 *
 * Now it's a non-blocking tick like every other _UI_FUNC(): one pass per
 * ui_refresh(), no internal loop, no sleep (see the "no sleeps in here, they
 * will fuck up the Zephyr threads" TODO at the top of this file), and the
 * value lives in the driver's own backlight_pct rather than a stack local.
 */
#define BRIGHTNESS_STEP 5
#define BRIGHTNESS_MIN  5    /* never let the user black the backlight out entirely -
                              * at 0 the screen is unreadable and they can't see to
                              * turn it back up */
#define BRIGHTNESS_MAX  100

void system_adjust_brightness_UI_FUNC(void) {
    /* Seeded from the display driver's live value (st7735s_compat.c), not a
     * local - so re-entering this screen shows the brightness actually in
     * effect, including one set on a previous visit or preserved across a
     * sleepIn/sleepOut. */
    uint8_t brightness = backlight_pct;

    if (first_ui_time) {
        first_ui_time = false;
        button_buffer_clear();
        display_out_measurement("Brightness", brightness);
        return;
    }

    /* Same drain-then-redraw-once shape as the time/date setter above: step
     * the value for every queued press, but only push it to the backlight
     * and repaint the readout once, at the final value. */
    uint8_t btn_poll;
    bool changed = false;

    while ((btn_poll = get_button_event()) != 0) {
        // INCREMENT (SW1, top-left — UP, same as everywhere else in the UI)
        if (btn_poll == BUTTON_1_MASK) {
            if (brightness < BRIGHTNESS_MAX) {
                brightness = (brightness + BRIGHTNESS_STEP > BRIGHTNESS_MAX)
                                ? BRIGHTNESS_MAX : brightness + BRIGHTNESS_STEP;
                changed = true;
            }
        }
        // DECREMENT (SW4, bottom-left)
        else if (btn_poll == BUTTON_4_MASK) {
            if (brightness > BRIGHTNESS_MIN) {
                brightness = (brightness - BRIGHTNESS_STEP < BRIGHTNESS_MIN)
                                ? BRIGHTNESS_MIN : brightness - BRIGHTNESS_STEP;
                changed = true;
            }
        }
    }

    if (changed) {
        /* Honour the low-power cap: in LP mode the user can still turn the
         * backlight DOWN, but not back above the cap. Showing the requested
         * value while driving a capped one would be a lie, so display what is
         * actually in effect. */
        uint8_t applied = low_power_cap_backlight(brightness);

        Backlight_Pct(applied);
        display_out_measurement("Brightness", applied);
    }

    return;
}

/*
 * system_low_power_UI_FUNC: low-power mode toggle.
 *
 *   SW1 (top-left)     turn low-power mode ON
 *   SW4 (bottom-left)  turn low-power mode OFF
 *
 * Reads 1/0 rather than a name because display_out_measurement() renders a
 * label plus an integer, matching the Brightness screen next to it.
 *
 * Note this toggles only the USER setting. The battery trigger
 * (low_power.h, engages under 3.60 V, releases over 3.90 V) is independent and
 * can hold the mode on even when the user setting reads 0 — which is why the
 * readout shows the EFFECTIVE state, not the user bit. Turning it "off" on a
 * flat battery therefore correctly appears to do nothing.
 */
/* Same shape as system_low_power_UI_FUNC below: UP enables, DOWN disables,
 * and the screen only repaints when something actually changed. */
void system_ble_UI_FUNC(void) {
    if (first_ui_time) {
        first_ui_time = false;
        button_buffer_clear();
        display_out_measurement("Bluetooth", ble_is_enabled() ? 1 : 0);
        return;
    }

    uint8_t btn_poll;
    bool changed = false;

    while ((btn_poll = get_button_event()) != 0) {
        if (btn_poll == BUTTON_1_MASK) {
            ble_set_enabled(true);
            changed = true;
        } else if (btn_poll == BUTTON_4_MASK) {
            ble_set_enabled(false);
            changed = true;
        }
    }

    if (changed) {
        /* Reads back ble_is_enabled() rather than echoing the request —
         * ble_set_enabled() refuses if the stack never came up, and the screen
         * should show what is true, not what was asked for. */
        display_out_measurement("Bluetooth", ble_is_enabled() ? 1 : 0);
    }
}


void system_low_power_UI_FUNC(void) {
    if (first_ui_time) {
        first_ui_time = false;
        button_buffer_clear();
        display_out_measurement("Low Power", low_power_is_active() ? 1 : 0);
        return;
    }

    uint8_t btn_poll;
    bool changed = false;

    while ((btn_poll = get_button_event()) != 0) {
        if (btn_poll == BUTTON_1_MASK) {
            low_power_set_user(true);
            changed = true;
        } else if (btn_poll == BUTTON_4_MASK) {
            low_power_set_user(false);
            changed = true;
        }
    }

    if (changed) {
        display_out_measurement("Low Power", low_power_is_active() ? 1 : 0);
    }
}

void system_clear_faults_UI_FUNC(void) {
    // No-op: this board uses a discrete BQ24090 + separate over/undervoltage
    // protection IC, neither with an I2C fault register to clear (see power.c).
    // Previously called into the BQ25120A driver, which has been removed —
    // that part isn't in this design.
}



/////////////////////////////////////////////////////
////////// MENU 1 - IMU SETTINGS   ///////// ////////
/////////////////////////////////////////////////////

void imuRead_UI_FUNC(void) {
    inv_imu_sensor_event_t event;
    if (!imu_get_latest_event(&event)) {
        return;  // no FIFO event has arrived yet - leave the screen as-is
    }
    display_out_imu(&event, IMU_DISPLAY_BOTH);
    return;
}


/* 8 rows fit under the title and column header at FONT_SMALL -- see
 * display_out_temp_list() for the line arithmetic. */
#define TEMP_LIST_SAMPLES 8

/*
 * The old version of this screen called display_out_measurement("IMU temp",
 * imu_get_temp()) -- and imu_get_temp() returns a FLOAT, which was being
 * passed to an int parameter, so every reading was truncated to whole degrees
 * before it ever reached the screen. One number, no history, no precision, and
 * no way to see the MCU die beside it.
 *
 * Now: the last N samples, newest first, IMU next to MCU.
 */
void imutempRead_UI_FUNC() {
    static uint32_t last_drawn_rev;

    /* Same redraw-on-change guard as the graph: the ring only advances on the
     * sensor tick (~9 s), so repainting on every 1 Hz ui_refresh() would be
     * eight wasted full-screen repaints out of nine. */
    uint32_t rev = temp_history_get_rev();
    if (!first_ui_time && rev == last_drawn_rev) {
        return;
    }
    first_ui_time = false;
    last_drawn_rev = rev;

    int16_t imu_raw[TEMP_LIST_SAMPLES];
    int16_t soc_centi[TEMP_LIST_SAMPLES];
    size_t  n = temp_history_get_pairs(imu_raw, soc_centi, TEMP_LIST_SAMPLES);

    if (n == 0) {
        display_out_notice("TEMP LOG", "No samples yet.",
                           imu_alive ? "Wait ~9s." : "IMU is not up.");
        return;
    }

    display_out_temp_list(imu_raw, soc_centi, n);
}


void pedometer_UI_FUNC(void) {
    display_out_measurement("Steps", (int)step_count);
    return;
}


/* Plenty for a 128px-wide plot (each sample gets >1px) - doesn't need to
 * match TEMP_HISTORY_LEN in imu.c, temp_history_get() just copies up to
 * however many it's asked for. */
#define TEMP_GRAPH_SAMPLES 60

/* Left margin reserved for the min/max Fahrenheit labels, so they sit
 * beside the plot box instead of overlapping the line - see the comment
 * above drawGraph() in display.h for why the box itself can't be scaled
 * from data automatically. */
#define TEMP_GRAPH_LABEL_MARGIN 32
#define TEMP_GRAPH_LABEL_FONT   FONT_SMALL

void tempGraph_UI_FUNC(void) {
    static uint32_t last_drawn_rev;

    /* Redraw only when new data has actually landed since the last draw (or
     * this is the first draw since entering the screen) - a line graph can't
     * be partially redrawn the way the clock/stopwatch digit fields are, so
     * the cheapest available optimization is skipping the redraw entirely
     * when nothing changed, rather than re-plotting the same points on
     * every ui_refresh() tick. */
    uint32_t rev = temp_history_get_rev();
    if (!first_ui_time && rev == last_drawn_rev) {
        return;
    }
    first_ui_time = false;
    last_drawn_rev = rev;

    int16_t samples[TEMP_GRAPH_SAMPLES];
    size_t n = temp_history_get(samples, TEMP_GRAPH_SAMPLES);

    if (n < 2) {
        /* This used to be display_out_measurement("Temp Graph", 0), which
         * draws a title over a big "0" -- visually identical to a real reading
         * and the reason this screen looked broken rather than empty. Say what
         * is actually wrong instead.
         *
         * The distinction matters: an IMU that never came up will NEVER fill
         * this ring, so "wait" would be a lie on that board. */
        if (!imu_alive) {
            display_out_notice("TEMP GRAPH", "No data: IMU is", "not running.");
        } else if (n == 0) {
            display_out_notice("TEMP GRAPH", "No samples yet.", "Wait ~9s.");
        } else {
            display_out_notice("TEMP GRAPH", "Need 2 samples", "to plot. Have 1.");
        }
        return;
    }

    int16_t y_min = samples[0];
    int16_t y_max = samples[0];
    for (size_t i = 1; i < n; i++) {
        if (samples[i] < y_min) y_min = samples[i];
        if (samples[i] > y_max) y_max = samples[i];
    }
    if (y_min == y_max) {  // drawGraph requires y_max > y_min
        y_min--;
        y_max++;
    }

    uint32_t box_top = 4;
    uint32_t box_bottom = HEIGHT - 4;
    uint32_t plot_left = 4 + TEMP_GRAPH_LABEL_MARGIN;

    /* drawGraph() only clears its own box - it's a generic primitive, not
     * a full-screen owner (see display.h). This screen previously relied
     * on that box happening to cover almost the entire panel, which left a
     * thin unclearable border; once the label margin shrank the box, the
     * whole left strip (and whatever the previous screen - the menu - left
     * there) was exposed. Every other full-screen UI function clears itself
     * on entry (display_out_data_stats, display_out_measurement, etc.) -
     * this one needs to do the same. */
    clear_display();

    drawGraph(samples, n, y_min, y_max, plot_left, box_top, WIDTH - 4, box_bottom);

    /* Min/max/mid labels in the reserved left margin - same raw->Fahrenheit
     * conversion imu_get_temp() uses for the live reading, applied to the
     * historical extremes (and their midpoint) instead. One decimal place -
     * a bare integer was throwing away resolution the graph's own vertical
     * scale doesn't have to lose. Drawn after drawGraph() so they aren't
     * immediately overwritten by its own background clear. */
    char label_max[10];
    char label_mid[10];
    char label_min[10];
    int16_t y_mid = y_min + (y_max - y_min) / 2;
    snprintf(label_max, sizeof(label_max), "%.1fF", (double)imu_raw_to_fahrenheit(y_max));
    snprintf(label_mid, sizeof(label_mid), "%.1fF", (double)imu_raw_to_fahrenheit(y_mid));
    snprintf(label_min, sizeof(label_min), "%.1fF", (double)imu_raw_to_fahrenheit(y_min));

    uint32_t box_mid_y = box_top + (box_bottom - box_top) / 2 - TEMP_GRAPH_LABEL_FONT / 2;

    printFieldLeftAligned(label_max, box_top, 2, TEMP_GRAPH_LABEL_MARGIN - 2, TEMP_GRAPH_LABEL_FONT);
    printFieldLeftAligned(label_mid, box_mid_y, 2, TEMP_GRAPH_LABEL_MARGIN - 2, TEMP_GRAPH_LABEL_FONT);
    printFieldLeftAligned(label_min, box_bottom - TEMP_GRAPH_LABEL_FONT, 2, TEMP_GRAPH_LABEL_MARGIN - 2, TEMP_GRAPH_LABEL_FONT);
}


/////////////////////////////////////////////////////
////////// MENU 2 - DATA /////////////////////////////
/////////////////////////////////////////////////////

void data_stats_UI_FUNC(void) {
    if (!nvs_ready()) {
        /* Was display_out_measurement("NVS", -1) — a bare "-1" under a title,
         * which reads as a value rather than as a failure. */
        display_out_notice("LOG STATS", "NVS not ready.", "No flash log.");
        return;
    }

    /* first_ui_time drives the one-shot full redraw (clear + static labels);
     * every later tick only repaints values that actually changed. Consuming
     * the flag here is the same pattern the other _UI_FUNC()s use. */
    bool full_redraw = first_ui_time;
    first_ui_time = false;

    display_out_data_stats((uint64_t)nvs_get_addr_offset(),
                           nvs_get_data_capacity(),
                           nvs_get_metadata_seq(),
                           full_redraw);
    return;
}


/*
 * erase_flash_UI_FUNC: wipe the NAND from the watch itself.
 *
 * The risk here is not the code path, it is a stray press landing on a
 * destructive menu item. So the guard is a SHAPE, not a warning:
 *
 *   - The cursor opens on Cancel, which is also listed first. The first
 *     SELECT after entering can only cancel.
 *   - Committing needs UP or DOWN (a DIFFERENT physical button) and then
 *     SELECT. No amount of repeated or bouncing presses on one button can
 *     reach the erase, and the two-press sequence SELECT,SELECT exits.
 *   - The screen states how much data dies, so a wrong confirm looks wrong.
 *
 * The erase itself is synchronous and takes a while; the screen says so
 * before it starts, because the UI is frozen for the duration.
 */
void erase_flash_UI_FUNC(void) {
    static int cursor;

    if (!nvs_ready()) {
        display_out_notice("ERASE FLASH", "NVS not ready.", "Nothing to erase.");
        return;
    }

    if (first_ui_time) {
        first_ui_time = false;
        /* Drop the SELECT that opened this screen — otherwise it is still
         * sitting in the buffer and gets applied to the cursor below, which
         * would defeat the whole point of starting on Cancel. */
        button_buffer_clear();
        cursor = 0;
        display_out_erase_confirm((uint64_t)nvs_get_addr_offset(), cursor, true);
        return;
    }

    uint8_t btn_poll;
    bool changed = false;

    while ((btn_poll = get_button_event()) != 0) {
        if (btn_poll == BUTTON_1_MASK || btn_poll == BUTTON_4_MASK) {
            cursor = (cursor == 0) ? 1 : 0;
            changed = true;
        }
        else if (btn_poll == BUTTON_2_MASK) {   /* SELECT */
            if (cursor != 1) {
                /* Cancel — back to the sub-menu we came from, same as BACK. */
                button_buffer_clear();
                ui_menu_return_to_sub_menu();
                ui_mode = UI_MODE_MENU;
                return;
            }

            /* Committed. Say so before blocking — nvs_erase_chip() is a
             * synchronous full-chip erase and nothing will repaint until it
             * returns. A frozen screen with no explanation looks like a
             * crash, which is the last thing you want a user to see while
             * their flash is mid-erase. */
            display_out_notice("ERASING...", "Do not power off.", NULL);

            LOG_WRN("erase: chip erase requested from the device UI");
            nvs_erase_chip();
            LOG_INF("erase: complete");

            button_buffer_clear();
            display_out_notice("ERASED", "Log cleared.", "Rate cfg reset.");
            return;
        }
    }

    if (changed) {
        display_out_erase_confirm((uint64_t)nvs_get_addr_offset(), cursor, false);
    }
}


/////////////////////////////////////////////////////
////////// MENU 3 - TIMER ////////////////////////////
/////////////////////////////////////////////////////

// stopwatch state - deliberately NOT touched by reset_uifunc_params() so
// elapsed time survives leaving/re-entering the screen; only SW4 clears it
static bool stopwatch_running = false;
static int64_t stopwatch_start_uptime;
static uint32_t stopwatch_elapsed_ms = 0;

void stopwatch_UI_FUNC(void) {
    uint8_t btn_poll = get_button_event();

    // START/PAUSE (button 1)
    if (btn_poll == BUTTON_1_MASK) {
        if (stopwatch_running) {
            stopwatch_elapsed_ms += (uint32_t)(k_uptime_get() - stopwatch_start_uptime);
            stopwatch_running = false;
        }
        else {
            stopwatch_start_uptime = k_uptime_get();
            stopwatch_running = true;
        }
    }
    // RESET (button 4)
    else if (btn_poll == BUTTON_4_MASK) {
        stopwatch_running = false;
        stopwatch_elapsed_ms = 0;
    }

    uint32_t elapsed = stopwatch_elapsed_ms;
    if (stopwatch_running) {
        elapsed += (uint32_t)(k_uptime_get() - stopwatch_start_uptime);
    }

    display_out_stopwatch(elapsed, stopwatch_running, stopwatch_screen_dirty);
    stopwatch_screen_dirty = false;
    return;
}


/* /\** */
/*  * tempRead_UI_FUNC: records the temp and displays it on the screen */
/*  *\/ */
/* void tempRead_UI_FUNC() */
/* { */
/*     // accompany text */
/*     char * text = "Temp (C): "; */

/*     // get value and turn into string */
/*     float temp = getTempValue(); */
/*     char pstring[6]; */
/*     floatToString(temp, &pstring, 5, 2); */

/*     // display measurement */
/*     displayMeasurement(text, &pstring); */
/* } */


/*
 * activity_UI_FUNC: the activity session screen.
 *
 * UP/DOWN move the cursor, SELECT toggles the highlighted activity. BACK is
 * not handled here — handle_ui_input() already treats it generically as
 * "return to the sub-menu list" for every leaf screen.
 *
 * Toggle rather than separate start/stop entries: with the catalogue meant to
 * grow to a dozen-plus activities, two rows each would double the list for no
 * information gain. The running one is marked "*" in the list, so what SELECT
 * will do is always visible.
 */
void activity_UI_FUNC(void) {
    uint8_t btn = get_button_event();
    size_t  count = activity_count();

    if (count > 0) {
        if (btn == BUTTON_1_MASK) {            /* SW1 top-left = UP */
            if (activity_cursor > 0) {
                activity_cursor--;
                activity_screen_dirty = true;  /* cursor moved: repaint rows */
            }
        }
        else if (btn == BUTTON_4_MASK) {       /* SW4 bottom-left = DOWN */
            if (activity_cursor + 1 < count) {
                activity_cursor++;
                activity_screen_dirty = true;
            }
        }
        else if (btn == BUTTON_2_MASK) {       /* SW2 top-right = SELECT */
            activity_toggle(activity_id_at(activity_cursor));
        }
    }

    if (activity_cursor >= count) {
        activity_cursor = 0;   /* catalogue shrank under us; stay in range */
    }

    display_out_activity(activity_cursor, activity_screen_dirty);
    activity_screen_dirty = false;
}
