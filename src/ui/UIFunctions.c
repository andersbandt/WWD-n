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

/* My header files */
#include <hardware/button.h>
#include <imu.h>
#include <peripheral/clock.h>
#include <peripheral/rv3028.h>
#include <memory/nvs.h>

/* UI and display */
#include <display.h>
#include <ui.h>


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
 *   SW1 (top-left)     DECREMENT the selected field
 *   SW4 (bottom-left)  INCREMENT the selected field
 *   SW2 (top-right)    NEXT SCREEN (TIME -> DATE -> commit & exit)
 *   SW3 (bottom-right) NEXT FIELD within the current screen (wraps)
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

    // Get button event from buffer instead of polling directly
    uint8_t btn_poll = get_button_event();

    // DECREMENT (SW1, top-left)
    if (btn_poll == BUTTON_1_MASK) {
        adjust_selected_field(DIR_DOWN);
        draw_time_set_screen();
    }
    // INCREMENT (SW4, bottom-left)
    else if (btn_poll == BUTTON_4_MASK) {
        adjust_selected_field(DIR_UP);
        draw_time_set_screen();
    }
    // NEXT FIELD within this screen (SW3, bottom-right)
    else if (btn_poll == BUTTON_3_MASK) {
        position = (position + 1) % SET_FIELDS_PER_SCREEN;
        draw_time_set_screen();
    }
    // NEXT SCREEN (SW2, top-right)
    else if (btn_poll == BUTTON_2_MASK) {
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
        }
    }

    return;
}



/*
 * system_adjust_brightness_UI_FUNC: backlight brightness screen.
 *
 *   SW1 (top-left)     DECREMENT brightness
 *   SW4 (bottom-left)  INCREMENT brightness
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

    uint8_t btn_poll = get_button_event();
    bool changed = false;

    // DECREMENT (SW1, top-left)
    if (btn_poll == BUTTON_1_MASK) {
        if (brightness > BRIGHTNESS_MIN) {
            brightness = (brightness - BRIGHTNESS_STEP < BRIGHTNESS_MIN)
                            ? BRIGHTNESS_MIN : brightness - BRIGHTNESS_STEP;
            changed = true;
        }
    }
    // INCREMENT (SW4, bottom-left)
    else if (btn_poll == BUTTON_4_MASK) {
        if (brightness < BRIGHTNESS_MAX) {
            brightness = (brightness + BRIGHTNESS_STEP > BRIGHTNESS_MAX)
                            ? BRIGHTNESS_MAX : brightness + BRIGHTNESS_STEP;
            changed = true;
        }
    }

    if (changed) {
        Backlight_Pct(brightness);
        display_out_measurement("Brightness", brightness);
    }

    return;
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


void imutempRead_UI_FUNC() {
    int16_t imu_temp = imu_get_temp();
    display_out_measurement("IMU temp", imu_temp);
    return;
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
        display_out_measurement("Temp Graph", 0);  // not enough history yet
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
        display_out_measurement("NVS", -1);
        return;
    }

    display_out_data_stats(nvs_get_addr_offset(), nvs_get_metadata_seq());
    return;
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



