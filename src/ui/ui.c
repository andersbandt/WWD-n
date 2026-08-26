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
#include <power/low_power.h>
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


int bat_percent;
int charging_status;


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


void ui_clock_set_soc_temp(float temp)
{
    clock_data.soc_temp = temp;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_SOC_TEMP);
}


void ui_clock_set_ble(int on)
{
    clock_data.ble_on = on;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_BLE);
}


void ui_clock_set_worn(int worn)
{
    clock_data.worn = worn;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_WEAR);
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
    if (clock_data.charging_status == status) {
        return;  /* nothing to redraw — the indicator's text never changes */
    }
    clock_data.charging_status = status;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_POWER);
}

/**
 * @brief Set low-power (TPS63900 power-save) indicator state and mark dirty
 */
void ui_clock_set_low_power(int low_power)
{
    if (clock_data.low_power == low_power) {
        return;
    }
    clock_data.low_power = low_power;
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_POWER);
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

    /* Start the auto-off countdown from boot, not from the first button press —
     * an untouched device should put its screen to sleep on its own. */
    ui_note_activity();
}


void ui_refresh() {
    /* Nothing to draw into a sleeping panel — the ST7735S is in SLPIN with
     * the backlight at 0 (see switch_display()), so every draw here would be
     * SPI traffic nobody can see. Clock data still keeps updating in the
     * background; the wake path below repaints from it. */
    if (!display_is_awake()) {
        return;
    }

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

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_SOC_TEMP)) {
                display_out_soc_temp(clock_data.soc_temp);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_SOC_TEMP);
            }

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_BLE)) {
                display_out_ble_indicator(clock_data.ble_on);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_BLE);
            }

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_WEAR)) {
                display_out_wear_indicator(clock_data.worn);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_WEAR);
            }

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_BATTERY)) {
                display_out_battery(clock_data.bat_mv);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_BATTERY);
            }

            if (ui_clock_is_dirty(UI_CLOCK_DIRTY_POWER)) {
                /* "CHG" is fed by battery_charging(), still stubbed false —
                 * the badge is drawn anyway so the slot is reserved on screen
                 * (see display_out_power_indicators() in ui_display.c). */
                display_out_power_indicators(clock_data.charging_status,
                                             clock_data.low_power);
                ui_clock_clear_dirty(UI_CLOCK_DIRTY_POWER);
            }

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

        case UI_MODE_ADJUST_BRIGHTNESS:
            system_adjust_brightness_UI_FUNC();
            break;

        case UI_MODE_CLEAR_FAULTS:
            system_clear_faults_UI_FUNC();
            break;

        case UI_MODE_BLE:
            system_ble_UI_FUNC();
            break;

        case UI_MODE_LOW_POWER:
            system_low_power_UI_FUNC();
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

        case UI_MODE_ACTIVITY:
            activity_UI_FUNC();
            break;

        case UI_MODE_STOPWATCH:
            stopwatch_UI_FUNC();
            break;

        default:
            LOG_ERR("Unknown UI mode: %d", ui_mode);
            ui_mode = UI_MODE_CLOCK; // Fallback to clock mode
            ui_enter_clock_face();   // ...and put the background back with it
            break;
    }
    k_mutex_unlock(&display_draw_mutex);
}


/* ---------------------------------------------------------------------------
 * Display auto-off
 *
 * Deliberately driven from ui_refresh_thread's existing 1 s tick (main.c
 * calls ui_idle_tick() there) rather than by re-enabling display_timeout_thread
 * + timer3. That thread has been disabled since 2026-07-28 because its 512 B
 * stack overflowed under this NO_OPTIMIZATIONS build and took the board down
 * with it; the work here is a k_uptime_get() comparison plus, once per
 * timeout, a switch_display() call, so giving it its own 4 KB stack to sleep
 * in would cost RAM for nothing. A 1 s granularity on a 9 s timeout is fine.
 *
 * Wake-up is handled in handle_ui_input() below: the button that wakes the
 * screen is consumed by the wake and not also treated as navigation.
 * ------------------------------------------------------------------------- */
static int64_t last_activity_ms;

/* Defined below, alongside the display auto-off state it touches. Declared
 * here because ui_open_activity_screen() is the one caller that sits above it
 * in the file. (display_is_awake() needs no forward decl — it is public, in
 * display.h.) */
static void wake_display_and_repaint(void);

void ui_open_activity_screen(void)
{
    k_mutex_lock(&display_draw_mutex, K_FOREVER);

    /* A hold also counts as user activity — otherwise the display could time
     * out mid-session-start, since the hold itself is not a normal press. */
    ui_note_activity();

    if (!display_is_awake()) {
        wake_display_and_repaint();
    }

    ui_menu_force_exit();
    change_ui_mode(UI_MODE_ACTIVITY);

    k_mutex_unlock(&display_draw_mutex);
}


void ui_note_activity(void)
{
    last_activity_ms = k_uptime_get();
}

/* Activity timestamp sampled when the fade starts, so idle_fade_cancelled()
 * can spot ui_note_activity() landing mid-fade. */
static int64_t fade_start_activity_ms;

/* Polled once per fade step. A button press (or any other wake source) calls
 * ui_note_activity() from another thread, which moves last_activity_ms; that
 * is the abort signal.
 *
 * The read is not atomic — last_activity_ms is 64-bit and this is a Cortex-M4
 * — but all this asks is "did the value change". A torn read differs from the
 * sampled value just as a clean one does, so it still cancels; the failure
 * mode is a spurious cancel, which merely keeps the screen on one more tick. */
static bool idle_fade_cancelled(void)
{
    return last_activity_ms != fade_start_activity_ms;
}

void ui_idle_tick(void)
{
    if (ui_status == 0 || !display_is_awake()) {
        return;
    }

    /* Low-power mode shortens the auto-off. The panel costs ~2.1 mA even at
     * 0% backlight, so sleeping it sooner is the biggest lever the mode has. */
    uint32_t timeout_ms = low_power_is_active() ? LOW_POWER_TIMEOUT_MS
                                                : UI_DISPLAY_TIMEOUT_MS;

    if ((k_uptime_get() - last_activity_ms) < timeout_ms) {
        return;
    }

    /* Fade the backlight down before sleeping, so the screen dims away
     * instead of snapping off.
     *
     * Deliberately outside display_draw_mutex: the fade takes
     * UI_DISPLAY_FADE_MS and only touches the backlight PWM, so holding the
     * draw lock across it would stall button_handler_thread and
     * protocol_thread for a full second for no reason. It also has to be
     * interruptible — otherwise a button pressed 100 ms into the fade would
     * sit unhandled while the screen kept dimming, then wake a display the
     * user never actually saw go out.
     *
     * This does block ui_refresh_thread for the duration, which delays that
     * tick's nvs_pipeline_tick() by ~1 s. Harmless for record timestamps
     * (those come from get_dt_ticks(), i.e. real time) and for the IMU ring
     * (drained by button_handler_thread on INT1, not from here), but
     * nvs_pipeline_tick() paces temperature logging by counting *calls*, so
     * its cadence slips one second per timeout event. If that ever matters,
     * move nvs_pipeline_tick() ahead of ui_idle_tick() in
     * ui_refresh_thread_entry(). */
    fade_start_activity_ms = last_activity_ms;

    if (!display_fade_out(UI_DISPLAY_FADE_MS, idle_fade_cancelled)) {
        /* Woken mid-fade; display_fade_out() already restored the backlight. */
        return;
    }

    /* switch_display() drives the panel over SPI1 (backlight PWM + SLPIN),
     * so it needs the same serialization as every other draw path — see
     * display_draw_mutex above. */
    k_mutex_lock(&display_draw_mutex, K_FOREVER);

    /* Re-check under the lock. handle_ui_input() can have run between the
     * fade's last poll and here, in which case it already treated the display
     * as awake and drew to it — sleeping now would drop the frame the user is
     * looking at. */
    if (idle_fade_cancelled()) {
        ST7735S_backlightRestore();
        k_mutex_unlock(&display_draw_mutex);
        return;
    }

    switch_display(false);
    k_mutex_unlock(&display_draw_mutex);

    LOG_INF("display asleep after %u ms idle (+%u ms fade)",
            timeout_ms, UI_DISPLAY_FADE_MS);
}


/* Common body of "wake the display and repaint it," shared by
 * handle_ui_input()'s wake branch and ui_wake_display_if_asleep() below.
 * Callers must already hold display_draw_mutex and must have already
 * confirmed the display was asleep — this does not check or lock, on purpose,
 * so it can be dropped into handle_ui_input()'s existing critical section
 * without changing that function's mutex/return ordering (shaped by a live
 * GDB-diagnosed crash — see the display_draw_mutex comment above). */
static void wake_display_and_repaint(void) {
    switch_display(true);

    /* The ST7735S keeps its GRAM through SLPIN/SLPOUT, so the screen comes
     * back showing whatever it slept on — no clear_display() here (that
     * would flash the panel). What is stale is the *content*: everything
     * ui_refresh() skipped while asleep. Force a full repaint of the clock
     * face from the current data; other modes are best-effort via
     * ui_refresh(), same caveat as ui_show_dump_in_progress(). */
    if (ui_mode == UI_MODE_CLOCK) {
        display_clock_time_reset();
        ui_clock_mark_dirty(UI_CLOCK_DIRTY_ALL);
        draw_clock_title();
    }
    ui_refresh();
    ui_note_activity();
}


/* See doc comment in ui.h. */
void ui_wake_display_if_asleep(void) {
    k_mutex_lock(&display_draw_mutex, K_FOREVER);

    if (!display_is_awake()) {
        wake_display_and_repaint();
    }

    k_mutex_unlock(&display_draw_mutex);
}


/* Semantic button roles for menu/back navigation, used by handle_ui_input()
 * below. To swap which physical button performs which role (e.g. swap BACK
 * and SELECT), edit these four lines only - handle_ui_input() never compares
 * button_status against a raw BUTTON_n_MASK directly, only these names.
 *
 * NOT covered by this mapping: each leaf screen in UIFunctions.c (time/date
 * setter, brightness, stopwatch) has its own separate, hardcoded button
 * scheme (increment/decrement/next-field/etc.) - those are a different kind
 * of action entirely (not up/down/select/back) and are remapped by editing
 * that screen's own function.
 *
 * The SW3+SW4 "always home" combo below is deliberately left as raw physical
 * masks rather than these roles - it's a two-finger physical gesture tied to
 * the bottom row, not a semantic action, so it doesn't move if SELECT/BACK
 * get swapped. */
#define BUTTON_ACTION_UP     BUTTON_1_MASK   // SW1, top-left
#define BUTTON_ACTION_DOWN   BUTTON_4_MASK   // SW4, bottom-left
#define BUTTON_ACTION_SELECT BUTTON_2_MASK   // SW2, top-right
#define BUTTON_ACTION_BACK   BUTTON_3_MASK   // SW3, bottom-right

/* Physical button layout (SW1-4, clockwise from top-left) — see button.h:
 *   BUTTON_1_MASK = SW1 = top-left     = UP     (BUTTON_ACTION_UP)
 *   BUTTON_2_MASK = SW2 = top-right    = SELECT (BUTTON_ACTION_SELECT)
 *   BUTTON_3_MASK = SW3 = bottom-right = BACK   (BUTTON_ACTION_BACK)
 *   BUTTON_4_MASK = SW4 = bottom-left  = DOWN   (BUTTON_ACTION_DOWN)
 * SW3+SW4 (the bottom row) together always return to the clock face,
 * regardless of UI mode. */
void handle_ui_input() {
    handle_ui_input_latched(0);
}


/*
 * forced_mask != 0 means "this press was already observed, act on it" rather
 * than re-reading the buttons.
 *
 * That distinction matters because a caller may have spent time deciding what
 * the press was before getting here. service_button2_hold() (main.c) polls for
 * up to 600 ms to tell a long press from a short one — by the time it knows,
 * a short press has been RELEASED, so button_poll() would read 0 and the press
 * would be silently dropped. That regressed SELECT everywhere until it was
 * caught on hardware; do not "simplify" this back to an unconditional poll.
 */
void handle_ui_input_latched(uint8_t forced_mask) {
    uint8_t button_status = forced_mask ? forced_mask : button_poll();

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
     * say, also jump a menu position or fire SELECT). Sleep comes from
     * ui_idle_tick() above. Same repaint sequence as the WOM raise-to-wake
     * path (button_handler_thread_entry() -> ui_wake_display_if_asleep()),
     * factored into wake_display_and_repaint() above. */
    if (!display_is_awake()) {
        wake_display_and_repaint();
        k_mutex_unlock(&display_draw_mutex);
        return;
    }

    ui_note_activity();

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
        if (button_status == BUTTON_ACTION_UP) {
            updateMenuScreen(-1);
        }
        else if (button_status == BUTTON_ACTION_DOWN) {
            updateMenuScreen(1);
        }
        else if (button_status == BUTTON_ACTION_SELECT) {
            updateMenuScreen(2);
        }
        else if (button_status == BUTTON_ACTION_BACK) {
            if (get_in_sub_menu_state()) {
                returnMenu();  // step up: sub-menu list -> main menu list
            }
            else {
                // Already at the top level - back exits to the clock face,
                // same as the SW3+SW4 combo below, just reachable with one
                // button from here.
                ui_menu_force_exit();
                change_ui_mode(UI_MODE_CLOCK);
                k_mutex_unlock(&display_draw_mutex);
                return;
            }
        }
        k_mutex_unlock(&display_draw_mutex);
        return;
    }
    else if (ui_mode == UI_MODE_CLOCK) {
        // Any single button from the clock face opens the menu (the bottom-
        // row home combo above already returned before reaching here).
        change_ui_mode(UI_MODE_MENU);
    }
    else if (button_status == BUTTON_ACTION_BACK && ui_mode != UI_MODE_PROMPT_TIME) {
        /* BACK from a running leaf screen (Pedometer, Temp Graph, Data
         * Stats, Stopwatch, Brightness, Clear Faults, IMU Read/Temp) returns
         * to the sub-menu list it was launched from - one level up, not all
         * the way home like the SW3+SW4 combo above.
         *
         * Excluded: UI_MODE_PROMPT_TIME. That screen already uses this same
         * physical button for its own purpose - since the 2026-08-23
         * SELECT/BACK swap that's SW3 = NEXT FIELD (it was SW2 = NEXT SCREEN
         * before), see system_prompt_for_time_UI_FUNC() in UIFunctions.c.
         * The collision moved buttons but did not go away, so the exclusion
         * still stands; it remains the only leaf screen with its own use of
         * BUTTON_ACTION_BACK's physical button. */
        ui_menu_return_to_sub_menu();
        ui_mode = UI_MODE_MENU;  // direct assignment, not change_ui_mode() - see
                                  // ui_menu_return_to_sub_menu()'s doc comment
        k_mutex_unlock(&display_draw_mutex);
        return;
    }

    k_mutex_unlock(&display_draw_mutex);

    // Push non-zero button events to buffer for UI functions to consume
    if (button_status != 0) {
        button_buffer_push(button_status);
    }

    /* ...and service the screen right now, on this thread, rather than
     * leaving the event to sit until ui_refresh_thread's next 1 Hz tick.
     * That tick was the real source of the setter's clunkiness: leaf screens
     * popped exactly one event per second, so a press could take up to a
     * second to appear and a burst of ten presses took ten seconds to crawl
     * through. ui_refresh() still calls the same _UI_FUNC()s on the tick
     * (the stopwatch needs a time-driven redraw); they find an empty buffer
     * and no-op.
     *
     * Only for leaf screens: CLOCK and MENU are handled inline above and
     * have already drawn by the time we get here. ui_refresh() takes
     * display_draw_mutex itself, which is why this sits after the unlock. */
    if (ui_mode != UI_MODE_CLOCK && ui_mode != UI_MODE_MENU) {
        ui_refresh();
    }
}


/* True while the on-screen mode is one where holding a button down should
 * auto-repeat (the two value-editing screens). Used by
 * button_handler_thread_entry() in main.c; kept here so the mode list lives
 * next to ui_mode rather than being duplicated across files. */
bool ui_autorepeat_active(void) {
    return ui_mode == UI_MODE_PROMPT_TIME || ui_mode == UI_MODE_ADJUST_BRIGHTNESS;
}


/*
 * ui_enter_clock_face: restore the clock face's chrome and force a full repaint.
 *
 * Factored out because three separate paths return to the clock and only one
 * of them goes through change_ui_mode(): the time-setter commits and assigns
 * ui_mode directly (UIFunctions.c), and ui_refresh() falls back here on an
 * unknown mode. Each of those has to put the background back, or the clock
 * face draws on the menu's pink and stays that way until the next real mode
 * change.
 *
 * Does NOT call ui_refresh() — callers differ on whether they want the repaint
 * now or on the next tick, and ui_refresh() takes display_draw_mutex, which
 * some callers already hold.
 */
void ui_enter_clock_face(void)
{
    display_set_default_background();
    clear_display();
    draw_clock_title();
    display_clock_time_reset();               /* screen was just cleared */
    ui_clock_mark_dirty(UI_CLOCK_DIRTY_ALL);  /* badges repaint too */
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

    /* Background follows the mode. Set BEFORE anything draws, so the wipe
     * below and every box-clear afterwards use the right colour.
     *
     * The leaf screens (activity, log stats, stopwatch, ...) do not need a
     * wipe here — each already clears itself via its full_redraw path, which
     * now picks up whichever background is current. Only the menu did not,
     * which is why the clock face's badges used to show through behind it. */
    if (ui_mode != UI_MODE_CLOCK) {
        display_set_menu_background();
    }
    /* The clock case is handled by ui_enter_clock_face() below, which owns
     * restoring the default background along with the rest of the chrome. */

    // update screen based on new mode
    if (ui_mode == UI_MODE_CLOCK) {
        initMenu();
        ui_enter_clock_face();
        ui_refresh();
    }
    else if (ui_mode == UI_MODE_MENU) {
        /* Wipe the clock face first. Without this the menu drew ON TOP of it
         * and the battery voltage, both temperature badges, the step count
         * and the status badges stayed visible around the menu list. */
        clear_display();
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









