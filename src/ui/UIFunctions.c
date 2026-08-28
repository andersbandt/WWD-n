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
#include <power/power.h>
#include <util/steps_day.h>

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
/*
 * draw_setter_hints: names each of the four buttons, at the corner it sits at.
 *
 * The time/date setter is the one screen where every button does something
 * different from everywhere else in the UI — SW3 moves the cursor between
 * fields instead of going back, and SW2 commits instead of selecting — and
 * none of that is discoverable by looking at it. Anders asked for labels;
 * this puts them at the corners rather than in a list because the buttons ARE
 * at the corners, so a label beside a button needs no legend to decode.
 *
 * SW2's label changes with the screen, since its job does: it pages
 * TIME -> DATE, and then commits. "SAVE" appearing only on the last screen is
 * the cue that there is no third page.
 *
 * printField*Aligned() rather than printLine(): each clears its own fixed box
 * first, so "FIELD >" cannot leave a tail behind when it is replaced by a
 * shorter label, and each is banded so the hints do not flicker on the
 * redraw that every button press triggers.
 */
#define SETTER_HINT_FONT     FONT_SMALL
#define SETTER_HINT_TOP_Y    26    /* under the title, above the value */
#define SETTER_HINT_BOT_Y    138   /* clears the value, 12 px of glyph fits in 160 */
#define SETTER_HINT_W        50

static void draw_setter_hints(void)
{
    /* SW1 top-left / SW4 bottom-left: the left column changes the value, the
     * same convention the brightness screen uses. */
    printFieldLeftAligned("UP",   SETTER_HINT_TOP_Y, 2, SETTER_HINT_W, SETTER_HINT_FONT);
    printFieldLeftAligned("DOWN", SETTER_HINT_BOT_Y, 2, SETTER_HINT_W, SETTER_HINT_FONT);

    /* SW2 top-right: advance a screen, then finish. SW3 bottom-right: advance
     * a field within this screen. */
    printFieldRightAligned(set_screen == SET_SCREEN_TIME ? "DATE >" : "SAVE",
                           SETTER_HINT_TOP_Y, WIDTH - 2, SETTER_HINT_W, SETTER_HINT_FONT);
    printFieldRightAligned("FIELD >",
                           SETTER_HINT_BOT_Y, WIDTH - 2, SETTER_HINT_W, SETTER_HINT_FONT);
}


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

    draw_setter_hints();
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
                 * here or the clock face draws on the menu theme over the old text. */
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

/* Auto-repeat acceleration.
 *
 * The repeat itself already exists (run_button_autorepeat() in main.c: 450 ms
 * to engage, then a step every 120 ms). At a flat 5% per step, crossing the
 * range is 19 steps — over two seconds of holding, and it feels like the
 * screen is ignoring you.
 *
 * So the step grows the longer the hold lasts: 5, then 10, then 20. Small
 * steps stay available for the first few, which is what a tap needs, and a
 * sustained hold crosses 5..100 in about a second. The trigger is the GAP
 * between changes rather than a press count — a gap under the threshold can
 * only come from the repeat timer, never from a human tapping, so a series of
 * deliberate single taps never accelerates.
 */
#define BRIGHTNESS_ACCEL_GAP_MS  250  /* below this, it is the repeat timer */
#define BRIGHTNESS_ACCEL_MED     4    /* steps held before 10% */
#define BRIGHTNESS_ACCEL_FAST    8    /* steps held before 20% */

static uint8_t brightness_step_for(uint8_t consecutive)
{
    if (consecutive >= BRIGHTNESS_ACCEL_FAST) {
        return BRIGHTNESS_STEP * 4;
    }
    if (consecutive >= BRIGHTNESS_ACCEL_MED) {
        return BRIGHTNESS_STEP * 2;
    }
    return BRIGHTNESS_STEP;
}


void system_adjust_brightness_UI_FUNC(void) {
    /* Seeded from the display driver's live value (st7735s_compat.c), not a
     * local - so re-entering this screen shows the brightness actually in
     * effect, including one set on a previous visit or preserved across a
     * sleepIn/sleepOut. */
    uint8_t brightness = backlight_pct;

    static uint8_t consecutive;     /* repeats in the current hold */
    static int64_t last_change_ms;

    if (first_ui_time) {
        first_ui_time = false;
        button_buffer_clear();
        consecutive = 0;
        last_change_ms = 0;
        display_out_brightness(low_power_cap_backlight(brightness), true);
        return;
    }

    /* Drain every queued press before touching the panel: the value is cheap
     * to step and the redraw is not, so a burst must land as ONE repaint at
     * the final value rather than one per press. */
    uint8_t btn_poll;
    bool changed = false;

    while ((btn_poll = get_button_event()) != 0) {
        if (btn_poll != BUTTON_1_MASK && btn_poll != BUTTON_4_MASK) {
            continue;
        }

        int64_t now = k_uptime_get();
        if (now - last_change_ms <= BRIGHTNESS_ACCEL_GAP_MS) {
            if (consecutive < 255) {
                consecutive++;
            }
        } else {
            consecutive = 0;    /* a fresh press, not a continuing hold */
        }
        last_change_ms = now;

        uint8_t step = brightness_step_for(consecutive);

        // INCREMENT (SW1, top-left — UP, same as everywhere else in the UI)
        if (btn_poll == BUTTON_1_MASK) {
            if (brightness < BRIGHTNESS_MAX) {
                brightness = (brightness + step > BRIGHTNESS_MAX)
                                ? BRIGHTNESS_MAX : brightness + step;
                changed = true;
            }
        }
        // DECREMENT (SW4, bottom-left)
        else {
            if (brightness > BRIGHTNESS_MIN) {
                brightness = (brightness < BRIGHTNESS_MIN + step)
                                ? BRIGHTNESS_MIN : brightness - step;
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
        display_out_brightness(applied, false);
    }

    return;
}

/* ---------------------------------------------------------------------------
 * Binary setting screens
 *
 * All three below share one shape, and now one look: display_out_toggle()
 * draws a Garmin-style ring around the panel — green for on, red for off —
 * with the state in words inside it. They used to render as
 * display_out_measurement("Bluetooth", 1), i.e. a label over a digit, which
 * made a setting look exactly like a measurement and required the reader to
 * remember which way the encoding went.
 *
 *   SW1 (top-left)     turn it ON
 *   SW4 (bottom-left)  turn it OFF
 *
 * Each reads its state back from the owning module rather than echoing the
 * request: ble_set_enabled() refuses if the stack never came up, and both
 * low power and sub-screen auto-off can be forced on by something other than
 * the user. The screen should show what is true.
 *
 * Each also redraws when the EFFECTIVE state changes for a reason that is not
 * a button press — the battery trigger engaging low-power mode, which in turn
 * forces sub-screen auto-off on — hence the last-drawn compare rather than a
 * plain `changed` flag.
 * ------------------------------------------------------------------------- */

#define TOGGLE_ON_HINT   "UP:   turn ON"
#define TOGGLE_OFF_HINT  "DOWN: turn OFF"

/*
 * toggle_screen: the body every binary setting screen shares.
 *
 * @param label   what is being toggled
 * @param setter  applies the user's request
 * @param getter  reads the effective state back
 * @param last_drawn caller-owned last-drawn state; -1 forces a draw
 */
static void toggle_screen(const char *label, void (*setter)(bool),
                          bool (*getter)(void), int8_t *last_drawn)
{
    if (first_ui_time) {
        first_ui_time = false;
        button_buffer_clear();
        *last_drawn = -1;
    }

    uint8_t btn_poll;
    while ((btn_poll = get_button_event()) != 0) {
        if (btn_poll == BUTTON_1_MASK) {
            setter(true);
        } else if (btn_poll == BUTTON_4_MASK) {
            setter(false);
        }
    }

    int8_t state = getter() ? 1 : 0;
    if (state == *last_drawn) {
        return;
    }
    *last_drawn = state;

    display_out_toggle(label, state != 0, TOGGLE_ON_HINT, TOGGLE_OFF_HINT);
}


static void ble_set(bool on) { ble_set_enabled(on); }
static bool ble_get(void)    { return ble_is_enabled(); }

void system_ble_UI_FUNC(void) {
    static int8_t last_drawn = -1;
    toggle_screen("Bluetooth", ble_set, ble_get, &last_drawn);
}


/*
 * Note this toggles only the USER setting. The battery trigger (low_power.h,
 * engages under 3.60 V, releases over 3.90 V) is independent and can hold the
 * mode on even when the user setting reads off — which is why the ring shows
 * the EFFECTIVE state. Turning it "off" on a flat battery therefore correctly
 * appears to do nothing.
 */
void system_low_power_UI_FUNC(void) {
    static int8_t last_drawn = -1;
    toggle_screen("Low Power", low_power_set_user, low_power_is_active, &last_drawn);
}


/*
 * system_sub_auto_off_UI_FUNC: does the display time out while you are INSIDE
 * a sub-screen (a graph, live IMU readings, log stats)?
 *
 * Off by default: those screens exist to be watched, and the 9 s clock-face
 * timeout blanks them mid-look. On, they behave like the clock face. Low-power
 * mode forces it on, same as it shortens every other timeout, so the ring can
 * read green with the user setting off — which is the honest answer to "will
 * my graph stay up right now". See ui_sub_auto_off_active() in ui.c.
 */
void system_sub_auto_off_UI_FUNC(void) {
    static int8_t last_drawn = -1;
    toggle_screen("Sub Auto-Off", ui_set_sub_auto_off, ui_sub_auto_off_active,
                  &last_drawn);
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

/* Gyro trace length. Caps at the ring's own depth (imu.c's
 * GYRO_HISTORY_LEN); more than the ~96 px plot is wide only costs the
 * per-column aggregation drawGraphMulti() already does. */
#define IMU_LIVE_GYRO_SAMPLES 64

/*
 * imuRead_UI_FUNC: the live IMU screen.
 *
 * What it used to be: display_out_imu(&event, IMU_DISPLAY_BOTH), which
 * printed AX, AY and GZ — the "compact tradeoff" branch of a display mode
 * enum, i.e. two thirds of the accelerometer and one third of the gyro, with
 * no time axis and a full clear_display() at 1 Hz. Three of six axes were
 * simply unreachable from the UI.
 *
 * What it is now: every axis of both sensors, each in the form that suits it
 * — see display_out_imu_live(). This function's job is only to gather the
 * two shapes of data and the time labels.
 *
 * Redrawn on every tick rather than on a revision change, unlike the graph
 * screens: the accel gauges ARE the live reading, so skipping a frame because
 * no new gyro sample landed would freeze the half of the screen that is
 * supposed to move. The per-row bands are what keep that flicker-free.
 */
void imuRead_UI_FUNC(void) {
    static int16_t gyro_x[IMU_LIVE_GYRO_SAMPLES];
    static int16_t gyro_y[IMU_LIVE_GYRO_SAMPLES];
    static int16_t gyro_z[IMU_LIVE_GYRO_SAMPLES];

    /* What is currently on the screen. Three states rather than a bool
     * because this screen has two of them to leave alone: the "no data yet"
     * notice and the live chrome. Coming back from the notice has to repaint
     * the chrome even though it is no longer the first frame on the screen. */
    enum { DRAWN_NOTHING = 0, DRAWN_NOTICE, DRAWN_LIVE };
    static uint8_t drawn_state;

    if (first_ui_time) {
        first_ui_time = false;
        drawn_state = DRAWN_NOTHING;
    }

    inv_imu_sensor_event_t event;
    if (!imu_get_latest_event(&event)) {
        /* Nothing has come out of the FIFO yet. Say so rather than drawing
         * three zeroed gauges, which would be indistinguishable from a watch
         * in free fall — the same "an empty state must not look like a
         * reading" rule the temperature screens learned. */
        if (drawn_state != DRAWN_NOTICE) {
            display_out_notice("IMU LIVE",
                               imu_alive ? "No FIFO data yet." : "IMU is not up.",
                               imu_alive ? "Wait a moment." : NULL);
            drawn_state = DRAWN_NOTICE;
        }
        return;
    }

    struct imu_live_view view = {
        .accel = { event.accel[0], event.accel[1], event.accel[2] },
        .full_redraw = (drawn_state != DRAWN_LIVE),
    };

    view.gyro_n = imu_gyro_history_get(gyro_x, gyro_y, gyro_z, IMU_LIVE_GYRO_SAMPLES);
    view.gyro_x = gyro_x;
    view.gyro_y = gyro_y;
    view.gyro_z = gyro_z;

    char t_left[8], t_mid[8], t_right[8];
    if (view.gyro_n >= 2) {
        /* Seconds-ago rather than clock times: the window is ~10 s wide, so
         * three absolute labels would all read the same minute. */
        uint32_t span_s = imu_gyro_history_span_s(view.gyro_n);
        graph_format_ago(span_s, t_left, sizeof(t_left));
        graph_format_ago(span_s / 2, t_mid, sizeof(t_mid));
        graph_format_ago(0, t_right, sizeof(t_right));
        view.x_left = t_left;
        view.x_mid = t_mid;
        view.x_right = t_right;
    }

    display_out_imu_live(&view);
    drawn_state = DRAWN_LIVE;
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

/* The plot box every graph screen draws into: the full panel minus a 4 px
 * frame. drawGraphEx() reserves its own axis margins INSIDE this, which is
 * why the screens no longer subtract a label margin themselves (they used to,
 * and the strip they left beside the plot showed the previous screen). */
#define GRAPH_BOX  ((struct graph_box){ 4, 4, (int16_t)(WIDTH - 4), (int16_t)(HEIGHT - 4) })


/*
 * graph_time_labels: fills three x-axis labels spanning `span_s` seconds back
 * from now.
 *
 * Absolute clock times when the RTC is set, relative offsets when it is not.
 * A graph without a time axis is the thing these screens were most obviously
 * missing — "the temperature went up" is a different claim from "the
 * temperature went up over the last nine minutes" — but an absolute label off
 * an unset clock is a confident lie, hence the fallback rather than a blank.
 *
 * Each buffer must hold at least 8 bytes.
 */
static void graph_time_labels(uint32_t span_s, char *left, char *mid, char *right,
                              size_t bufsz)
{
    if (rv3028_time_is_set()) {
        Time t = get_current_time();
        graph_format_clock((uint8_t)t.hours, (uint8_t)t.minutes, span_s, left, bufsz);
        graph_format_clock((uint8_t)t.hours, (uint8_t)t.minutes, span_s / 2, mid, bufsz);
        graph_format_clock((uint8_t)t.hours, (uint8_t)t.minutes, 0, right, bufsz);
    } else {
        graph_format_ago(span_s, left, bufsz);
        graph_format_ago(span_s / 2, mid, bufsz);
        graph_format_ago(0, right, bufsz);
    }
}


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
    graph_minmax(samples, n, &y_min, &y_max);
    graph_pad_range(&y_min, &y_max);

    /* Same raw->Fahrenheit conversion imu_get_temp() uses for the live
     * reading, applied to the historical extremes and their midpoint. One
     * decimal place - a bare integer throws away resolution the graph's own
     * vertical scale doesn't have to lose. */
    char label_max[10], label_mid[10], label_min[10];
    int16_t y_mid = (int16_t)(y_min + (y_max - y_min) / 2);
    snprintf(label_max, sizeof(label_max), "%.1fF", (double)imu_raw_to_fahrenheit(y_max));
    snprintf(label_mid, sizeof(label_mid), "%.1fF", (double)imu_raw_to_fahrenheit(y_mid));
    snprintf(label_min, sizeof(label_min), "%.1fF", (double)imu_raw_to_fahrenheit(y_min));

    /* The ring is fed by the sensor tick, so the span is (n-1) ticks wide.
     * Asking imu.c for it rather than assuming keeps this honest if that
     * cadence ever changes. */
    char t_left[8], t_mid[8], t_right[8];
    graph_time_labels(temp_history_span_s(n), t_left, t_mid, t_right, sizeof(t_left));

    struct graph_opts opts = {
        .style = GRAPH_LINE,
        .y_max_label = label_max,
        .y_mid_label = label_mid,
        .y_min_label = label_min,
        .x_left = t_left,
        .x_mid = t_mid,
        .x_right = t_right,
        .mark_column = -1,
    };

    clear_display();
    drawGraphEx(samples, n, y_min, y_max, GRAPH_BOX, &opts);
}


/*
 * batteryGraph_UI_FUNC: battery voltage over the last day.
 *
 * The y range is the DATA's range padded a little, not the cell's full
 * 3.0-4.2 V. A day of use moves the pack by a few tens of millivolts, and on
 * a full-cell axis that is a flat line — which is exactly the reading you
 * cannot get from the numeric badge on the clock face either, so the screen
 * would add nothing. The labels carry the absolute voltages, so an auto-range
 * cannot mislead about level, only about slope.
 */
void batteryGraph_UI_FUNC(void) {
    static uint32_t last_drawn_rev;

    uint32_t rev = battery_history_rev();
    if (!first_ui_time && rev == last_drawn_rev) {
        return;
    }
    first_ui_time = false;
    last_drawn_rev = rev;

    static int16_t samples[BATTERY_HISTORY_LEN];
    size_t n = battery_history_get(samples, BATTERY_HISTORY_LEN);

    if (n < 2) {
        /* At one stored point per 5 minutes, "no samples yet" is the normal
         * state for the first few minutes after a boot — say how long rather
         * than leaving the screen looking broken. */
        display_out_notice("BATTERY", n == 0 ? "No samples yet." : "Need 2 samples.",
                           "One per 5 min.");
        return;
    }

    int16_t y_min = samples[0];
    int16_t y_max = samples[0];
    graph_minmax(samples, n, &y_min, &y_max);

    /* Widen a nearly-flat trace to at least 50 mV so ADC dither does not get
     * magnified into a dramatic-looking discharge curve. */
    if (y_max - y_min < 50) {
        int16_t mid = (int16_t)(y_min + (y_max - y_min) / 2);
        y_min = (int16_t)(mid - 25);
        y_max = (int16_t)(mid + 25);
    }
    graph_pad_range(&y_min, &y_max);

    char label_max[10], label_mid[10], label_min[10];
    int16_t y_mid = (int16_t)(y_min + (y_max - y_min) / 2);
    snprintf(label_max, sizeof(label_max), "%d.%02d", y_max / 1000, (y_max % 1000) / 10);
    snprintf(label_mid, sizeof(label_mid), "%d.%02d", y_mid / 1000, (y_mid % 1000) / 10);
    snprintf(label_min, sizeof(label_min), "%d.%02d", y_min / 1000, (y_min % 1000) / 10);

    char t_left[8], t_mid[8], t_right[8];
    graph_time_labels(battery_history_span_s(n), t_left, t_mid, t_right, sizeof(t_left));

    struct graph_opts opts = {
        .style = GRAPH_LINE,
        .y_max_label = label_max,
        .y_mid_label = label_mid,
        .y_min_label = label_min,
        .x_left = t_left,
        .x_mid = t_mid,
        .x_right = t_right,
        .mark_column = -1,
    };

    clear_display();
    drawGraphEx(samples, n, y_min, y_max, GRAPH_BOX, &opts);
}


/*
 * stepsGraph_UI_FUNC: steps per hour for today.
 *
 * Bars, not a line, and hours rather than a rolling window: steps are a rate
 * over an interval (see steps_day.h), and the question this screen answers is
 * "when did I move today", which a cumulative line cannot show at all.
 *
 * The x axis is fixed at 00:00-24:00 rather than scaled to the hours elapsed
 * so far, so the bars do not slide leftward as the day fills - a bar for 09:00
 * stays under the same pixel all day.
 */
void stepsGraph_UI_FUNC(void) {
    static uint32_t last_drawn_rev;

    uint32_t rev = steps_day_rev();
    if (!first_ui_time && rev == last_drawn_rev) {
        return;
    }
    first_ui_time = false;
    last_drawn_rev = rev;

    int16_t hours[STEPS_DAY_HOURS];
    size_t n = steps_day_hours(hours, STEPS_DAY_HOURS);

    if (n == 0) {
        /* The buckets only start once there is a real date to attribute them
         * to - see steps_day_update(). Naming the reason matters here because
         * the fix is a user action (sync the clock over BLE), not waiting. */
        display_out_notice("STEPS TODAY", "Clock not set.", "Sync time first.");
        return;
    }

    int16_t peak = 0;
    graph_minmax(hours, n, NULL, &peak);

    if (peak <= 0) {
        display_out_notice("STEPS TODAY", "No steps logged", "yet today.");
        return;
    }

    /* Headroom above the tallest bar so it does not touch the border, and a
     * floor of 100 so a 3-step hour is not drawn as a full-height bar. */
    int16_t y_max = (peak < 100) ? 100 : (int16_t)(peak + peak / 8);

    char label_max[10], label_mid[10];
    snprintf(label_max, sizeof(label_max), "%d", (int)y_max);
    snprintf(label_mid, sizeof(label_mid), "%d", (int)(y_max / 2));

    char total_label[12];
    snprintf(total_label, sizeof(total_label), "%u", (unsigned)steps_day_total());

    struct graph_opts opts = {
        .style = GRAPH_BAR,
        .y_max_label = label_max,
        .y_mid_label = label_mid,
        .y_min_label = "0",
        .x_left = "00",
        .x_mid = "12",
        .x_right = "24",
        /* The hour still filling is a partial count; outlining it stops a
         * half-finished 09:00 from reading as a genuinely quiet hour. */
        .mark_column = (int16_t)steps_day_current_hour(),
    };

    /* The day total gets its own header row ABOVE the plot rather than being
     * overlaid on it. Overlaying was the first attempt and it is a trap: the
     * top-right of the plot is where a busy evening's bars actually land, so
     * the label would have punched a background box through the data it was
     * describing. The bars answer "when", this answers "how many". */
    struct graph_box box = GRAPH_BOX;
    box.top = (int16_t)(box.top + FONT_SMALL + 2);

    clear_display();
    drawGraphEx(hours, n, 0, y_max, box, &opts);
    printFieldRightAligned(total_label, 4, WIDTH - 6, 60, FONT_SMALL);
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
 *
 * Since rate config moved to the nRF's internal flash (config_store.c) this no
 * longer destroys the user's sample rates -- erasing the log and resetting the
 * device are now separate actions, which is the point.
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
            display_out_notice("ERASED", "Log cleared.", "Settings kept.");
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
