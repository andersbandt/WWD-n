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
 * temp (top-right) and step count (bottom-right). Badges use
 * printFieldRightAligned() rather than printLine()/clearAndPrintLine()
 * because their digit count changes over time (e.g. temp 100 -> 9) —
 * clearAndPrintLine() only clears from the text's own left edge to the
 * screen's right edge, so a shrinking value leaves stale digits behind.
 * printFieldRightAligned() always clears the same fixed-size box first. */
#define CLOCK_TIME_LINE  1
#define CLOCK_TIME_X     8
#define CLOCK_TIME_FONT  FONT_XXLARGE

#define CLOCK_BADGE_FONT  FONT_SMALL
#define CLOCK_BADGE_WIDTH 44
#define CLOCK_TEMP_Y      4
#define CLOCK_STEPS_Y_MARGIN 20  /* from bottom of screen */

void display_out_time(Time time, time_invert_field_t invertField) {
    char time_str[15];
    sprintf(time_str, "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);

    if (invertField == TIME_INVERT_NONE) {
        // Main clock face: big, upper-middle, centered. Fixed-width format
        // (always "HH:MM:SS"), so clearAndPrintLine()'s ghosting risk doesn't apply.
        clearAndPrintLine(time_str, CLOCK_TIME_LINE, CLOCK_TIME_X, CLOCK_TIME_FONT);
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


void display_out_temp(int16_t temp) {
    char text[8];
    sprintf(text, "%dC", temp);
    printFieldRightAligned(text, CLOCK_TEMP_Y, WIDTH - 2,
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














