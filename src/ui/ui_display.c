//*****************************************************************************
//!
//! @file userInterface.c
//! @author Anders Bandt
//! @brief Provides user functionality through display
//! @version 1.0
//! @date September 2024
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


/* My header files */
#include <display.h>
#include <peripheral/clock.h>
#include <ui_display.h>





/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void clear_out_display() {
    clear_display();
}


/*
BELOW FUNCTIONS ARE FOR MAIN DISPLAY OUTPUT (clock face)
*/

void display_out_bms(int charging, int battery_percent) {
    char text[24]; // only 21-22 chars possible in next line
    sprintf(text, "CHG[%d] BAT[%d]", charging, battery_percent);
    printLine(text, 0, 0, FONT_LARGE);
}


/* Clock face layout: big centered time up top, small corner badges for
 * battery voltage (top-right), temp (bottom-left), and step count
 * (bottom-right). Badges use printFieldRightAligned()/printFieldLeftAligned()
 * rather than printLine()/clearAndPrintLine() because their digit count
 * changes over time (e.g. temp 100 -> 9) — clearAndPrintLine() only clears
 * from the text's own left edge to the screen's right edge, so a shrinking
 * value leaves stale digits behind (and would trample a neighboring badge
 * sharing the same row, e.g. temp/steps both on the bottom row). The
 * printField*Aligned() variants always clear the same fixed-size box first. */
#define CLOCK_TIME_LINE  1
#define CLOCK_TIME_X     8
#define CLOCK_TIME_FONT  FONT_XXLARGE

#define CLOCK_BADGE_FONT  FONT_SMALL
#define CLOCK_BADGE_WIDTH 44
#define CLOCK_BATTERY_Y   4       /* top-right badge */
#define CLOCK_TEMP_X      2       /* bottom-left badge */
#define CLOCK_STEPS_Y_MARGIN 20  /* from bottom of screen, shared by temp + steps row */

#define CLOCK_TIME_CHAR_W  (CLOCK_TIME_FONT / 2)   /* matches printFieldRightAligned's charWidth approximation */
#define CLOCK_TIME_FIELD_W (2 * CLOCK_TIME_CHAR_W) /* width of one 2-digit field: HH, MM, or SS */
/* Right edge of each field in drawText's fixed left-to-right layout starting
 * at CLOCK_TIME_X ("HH" then ":" then "MM" then ":" then "SS", each glyph —
 * including the colons — advancing by CLOCK_TIME_CHAR_W). */
#define CLOCK_HH_RIGHT (CLOCK_TIME_X + 2*CLOCK_TIME_CHAR_W)
#define CLOCK_MM_RIGHT (CLOCK_TIME_X + 5*CLOCK_TIME_CHAR_W)
#define CLOCK_SS_RIGHT (CLOCK_TIME_X + 8*CLOCK_TIME_CHAR_W)

/* File-scope (not function-local) so display_clock_time_reset() can force a
 * full redraw — needed whenever the screen was cleared out from under us
 * (e.g. returning to UI_MODE_CLOCK from the menu), see that function. */
static bool clock_time_initialized = false;
static int32_t clock_prev_h = -1, clock_prev_m = -1, clock_prev_s = -1;

/* Force the next display_out_time() call to do a full "HH:MM:SS" redraw
 * (including the colons) instead of a partial field update. Call this
 * whenever the screen may have been cleared without display_out_time()'s
 * knowledge (mode switches back to UI_MODE_CLOCK, including the first). */
void display_clock_time_reset(void)
{
    clock_time_initialized = false;
}

void display_out_time(Time time, time_invert_field_t invertField) {
    char time_str[15];
    sprintf(time_str, "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);

    if (invertField == TIME_INVERT_NONE) {
        /* Partial redraw: redrawing the full "HH:MM:SS" string every second
         * (even though usually only the seconds digits change) pushed ~4x
         * more pixels over SPI than necessary — flushBuffer() only sends the
         * dirty bounding box, so this panel visibly "wipes" left-to-right on
         * every full-string redraw. Only touch the 2-digit field(s) that
         * actually changed (via printTwoDigitFieldIfChanged(), shared with
         * display_out_stopwatch() below); colons are drawn once and never
         * touched again since they never change. */
        if (!clock_time_initialized) {
            clearAndPrintLine(time_str, CLOCK_TIME_LINE, CLOCK_TIME_X, CLOCK_TIME_FONT);
            clock_time_initialized = true;
            clock_prev_h = time.hours;
            clock_prev_m = time.minutes;
            clock_prev_s = time.seconds;
            return;
        }

        uint32_t posY = calculateLineY(CLOCK_TIME_LINE, CLOCK_TIME_FONT);
        printTwoDigitFieldIfChanged(time.hours, &clock_prev_h, posY, CLOCK_HH_RIGHT, CLOCK_TIME_FIELD_W, CLOCK_TIME_FONT);
        printTwoDigitFieldIfChanged(time.minutes, &clock_prev_m, posY, CLOCK_MM_RIGHT, CLOCK_TIME_FIELD_W, CLOCK_TIME_FONT);
        printTwoDigitFieldIfChanged(time.seconds, &clock_prev_s, posY, CLOCK_SS_RIGHT, CLOCK_TIME_FIELD_W, CLOCK_TIME_FONT);
        return;
    }

    // Time-setting UI function screen (system_prompt_for_time_UI_FUNC) — unrelated
    // layout, unchanged.
    // Time format: "HH:MM:SS"
    //   Hours:   indices 0-1
    //   Minutes: indices 3-4
    //   Seconds: indices 6-7
    int invertStart = -1;
    int invertEnd = -1;

    switch (invertField) {
        case TIME_INVERT_HOURS:
            invertStart = 0;
            invertEnd = 1;
            break;
        case TIME_INVERT_MINUTES:
            invertStart = 3;
            invertEnd = 4;
            break;
        case TIME_INVERT_SECONDS:
            invertStart = 6;
            invertEnd = 7;
            break;
        default:
            break;
    }

    printLineWithInversion(time_str, 2, 20, FONT_SMALL, invertStart, invertEnd);
}


void display_out_pedometer(int steps) {
    char text[15];
    sprintf(text, "%d", steps);
    printFieldRightAligned(text, HEIGHT - CLOCK_STEPS_Y_MARGIN, WIDTH - 2,
                            CLOCK_BADGE_WIDTH, CLOCK_BADGE_FONT);
}


void display_out_temp(float temp) {
    char text[10];
    /* imu_get_temp() (imu.c) converts the raw Celsius register value to
     * Fahrenheit before returning it — label accordingly, was mislabeled "C". */
    sprintf(text, "%.1fF", (double)temp);
    printFieldLeftAligned(text, HEIGHT - CLOCK_STEPS_Y_MARGIN, CLOCK_TEMP_X,
                           CLOCK_BADGE_WIDTH, CLOCK_BADGE_FONT);
}


void display_out_battery(int mv) {
    char text[10];
    /* Raw divider voltage, not state-of-charge — battery_percent()'s LiPo
     * discharge-curve mapping is a separate concern from validating that the
     * ADC/divider reading itself is sane, which is the point of this badge
     * right now (see project_mcp23008_bringup.md). */
    sprintf(text, "%.2fV", mv / 1000.0);
    printFieldRightAligned(text, CLOCK_BATTERY_Y, WIDTH - 2,
                            CLOCK_BADGE_WIDTH, CLOCK_BADGE_FONT);
}



/*
BELOW FUNCTIONS ARE SPECIALIZED AND LIKELY CALLED IN FROM A UI MENU FUNCTION
*/

void display_out_measurement(char * text, int value)
{
    char value_str[8];
    sprintf(value_str, "%d", value);

    clear_display();
    printLine(text, 2, 12, FONT_LARGE);  // print text displaying what kind of measurement it is
    printLine(value_str, 3, 12, FONT_LARGE);
    return;
}


void display_out_data_stats(int write_offset, uint32_t meta_seq)
{
    char offset_str[16];
    char seq_str[16];
    sprintf(offset_str, "Off:%d", write_offset);
    sprintf(seq_str, "Seq:%u", meta_seq);

    clear_display();
    printLine(offset_str, 2, 12, FONT_LARGE);
    printLine(seq_str, 3, 12, FONT_LARGE);
}


/*
 * display_out_stopwatch: redraws only the field(s) that actually changed
 * since the last call, instead of clear_display() + printLine on every tick
 * - the old version blanked and repainted the whole screen once a second
 * (visibly flickered); the next fix redrew the whole "MM:SS" string
 * whenever either digit pair changed (still ~2x the necessary SPI traffic
 * on every tick, since usually only SS changes). This version reuses
 * display_out_time()'s printTwoDigitFieldIfChanged() helper to independently
 * diff MM and SS the same way the clock face diffs HH/MM/SS - only the
 * 2-digit field that actually changed gets redrawn. The RUNNING/PAUSED
 * label is unrelated and still uses clearAndPrintLine(), which clears its
 * own line's background first so a shorter new string (e.g. "PAUSED"
 * replacing "RUNNING") can't leave stale trailing characters.
 *
 * full_redraw should be true only on first entry to this screen (the
 * caller is responsible for tracking that) - it does one clear_display()
 * and forces both fields to redraw, to wipe away whatever the previous
 * screen left behind and to reset the last-drawn-value tracking below.
 */
#define STOPWATCH_LABEL_LINE  2
#define STOPWATCH_LABEL_X     12
#define STOPWATCH_LABEL_FONT  FONT_LARGE

#define STOPWATCH_TIME_LINE   3
#define STOPWATCH_TIME_X      12
#define STOPWATCH_TIME_FONT   FONT_LARGE
#define STOPWATCH_TIME_CHAR_W (STOPWATCH_TIME_FONT / 2)
#define STOPWATCH_TIME_FIELD_W (2 * STOPWATCH_TIME_CHAR_W)
#define STOPWATCH_MM_RIGHT (STOPWATCH_TIME_X + 2*STOPWATCH_TIME_CHAR_W)
#define STOPWATCH_SS_RIGHT (STOPWATCH_TIME_X + 5*STOPWATCH_TIME_CHAR_W)

void display_out_stopwatch(uint32_t elapsed_ms, bool running, bool full_redraw)
{
    static bool last_running;
    static bool time_initialized = false;
    static int32_t last_mm = -1, last_ss = -1;  // shared MM/SS diffing state, see printTwoDigitFieldIfChanged()

    uint32_t total_sec = elapsed_ms / 1000;
    uint32_t mm = total_sec / 60;
    uint32_t ss = total_sec % 60;

    if (full_redraw) {
        clear_display();
        last_running = !running;    // force the label to redraw below
        time_initialized = false;   // force a full "MM:SS" redraw below
    }

    if (running != last_running) {
        clearAndPrintLine(running ? "RUNNING" : "PAUSED", STOPWATCH_LABEL_LINE, STOPWATCH_LABEL_X, STOPWATCH_LABEL_FONT);
        last_running = running;
    }

    /* First draw of this screen (or after full_redraw): paint the whole
     * "MM:SS" string once, including the colon, same as display_out_time()'s
     * clock_time_initialized path — after this, only the field(s) that
     * actually changed get touched via printTwoDigitFieldIfChanged(), shared
     * with the clock face above. */
    if (!time_initialized) {
        char time_str[16];  /* generous headroom for "%02u:%02u"; silences -Wformat-overflow */
        sprintf(time_str, "%02u:%02u", mm, ss);
        clearAndPrintLine(time_str, STOPWATCH_TIME_LINE, STOPWATCH_TIME_X, STOPWATCH_TIME_FONT);
        time_initialized = true;
        last_mm = mm;
        last_ss = ss;
        return;
    }

    uint32_t posY = calculateLineY(STOPWATCH_TIME_LINE, STOPWATCH_TIME_FONT);
    printTwoDigitFieldIfChanged(mm, &last_mm, posY, STOPWATCH_MM_RIGHT, STOPWATCH_TIME_FIELD_W, STOPWATCH_TIME_FONT);
    printTwoDigitFieldIfChanged(ss, &last_ss, posY, STOPWATCH_SS_RIGHT, STOPWATCH_TIME_FIELD_W, STOPWATCH_TIME_FONT);
}


void display_out_statistics(const int16_t *data, size_t num_data)
{
    char text[30];
    size_t len = 0;

    text[len++] = '|';
    text[len] = '\0';

    for (size_t i = 0; i < num_data; i++) {
        int written = snprintf(&text[len],
                               sizeof(text) - len,
                               "%d%s",
                               data[i],
                               (i + 1 < num_data) ? "," : "");

        if (written < 0 || written >= (int)(sizeof(text) - len)) {
            break;  // buffer full, stop safely
        }

        len += written;
    }

    clear_display();
    printLine(text, 1, 8, FONT_MEDIUM);
}


void display_out_imu(const inv_imu_sensor_event_t *event, imu_display_mode_t mode)
{
    char line0[12];
    char line1[12];
    char line2[12];

    if (event == NULL) {
        return;
    }

    clear_display();

    switch (mode) {

    case IMU_DISPLAY_ACCEL:
        snprintf(line0, sizeof(line0), "AX:%d", event->accel[0]);
        snprintf(line1, sizeof(line1), "AY:%d", event->accel[1]);
        snprintf(line2, sizeof(line2), "AZ:%d", event->accel[2]);
        break;

#if ICM_IS_GYRO_SUPPORTED
    case IMU_DISPLAY_GYRO:
        snprintf(line0, sizeof(line0), "GX:%d", event->gyro[0]);
        snprintf(line1, sizeof(line1), "GY:%d", event->gyro[1]);
        snprintf(line2, sizeof(line2), "GZ:%d", event->gyro[2]);
        break;
#endif

    case IMU_DISPLAY_BOTH:
#if ICM_IS_GYRO_SUPPORTED
        /* Compact: accel X/Y + gyro Z (example tradeoff) */
        snprintf(line0, sizeof(line0), "AX:%d", event->accel[0]);
        snprintf(line1, sizeof(line1), "AY:%d", event->accel[1]);
        snprintf(line2, sizeof(line2), "GZ:%d", event->gyro[2]);
#else
        snprintf(line0, sizeof(line0), "AX:%d", event->accel[0]);
        snprintf(line1, sizeof(line1), "AY:%d", event->accel[1]);
        snprintf(line2, sizeof(line2), "AZ:%d", event->accel[2]);
#endif
        break;

    default:
        snprintf(line0, sizeof(line0), "IMU ERR");
        line1[0] = '\0';
        line2[0] = '\0';
        break;
    }

    printLine(line0, 0, 0, FONT_LARGE);
    printLine(line1, 1, 0, FONT_LARGE);
    printLine(line2, 2, 0, FONT_LARGE);
}


void display_out_fault(int error_code)
{
    char text[12];
    sprintf(text, "Fault: [%d]", error_code);
    
    clear_display();
    printLine(text, 2, 12, FONT_LARGE);  // print text displaying what kind of measurement it is
    return;
}














