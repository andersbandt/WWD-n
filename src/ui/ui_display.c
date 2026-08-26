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
#include <string.h>
#include <unistd.h>


/* My header files */
#include <display.h>
#include <peripheral/clock.h>
#include <ui_display.h>
#include <activity/activity.h>
#include <imu.h>              /* imu_raw_to_fahrenheit() */
#include <peripheral/soc_temp.h>  /* soc_temp_centi_c_to_f() */





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

/* SoC die temperature, stacked directly above the IMU temp badge in the
 * bottom-left. 34 = CLOCK_STEPS_Y_MARGIN (20) + FONT_SMALL height (12) + 2 px
 * gap, so the two badges sit flush without touching. The band between the big
 * time display (which ends around y=74) and this row is otherwise empty, so
 * nothing else needs moving.
 *
 * Both temperature badges carry a source label ("M:" here for the MCU die,
 * "I:" on the IMU badge below) — two bare "78.2" values stacked on top of one
 * another are unreadable, and the whole reason both are on screen is to
 * compare them. Both carry the Fahrenheit unit. */
#define CLOCK_SOC_TEMP_Y_MARGIN 34

/* The two temperature badges get their own field width rather than sharing
 * CLOCK_BADGE_WIDTH (44 px) with the battery/steps badges: the labelled form
 * "M:78.2 F" is 8 chars = 48 px at FONT_SMALL's ~6 px/char, which overflows 44
 * and would clip silently (drawText has no wrap and no warning).
 *
 * 56 px is safe on both rows. The SoC badge has its row to itself. The IMU
 * badge shares its row with the right-aligned step count, which occupies
 * x 82..126 — so a left-aligned field from x=2 has until x=82 before it
 * collides, and 2+56 = 58 leaves 24 px of clearance. */
#define CLOCK_TEMP_FIELD_W 56

/* Bluetooth indicator: right-hand end of the SoC-temp row.
 *
 * Deliberately NOT on the LP/CX status row — that row's own comment records
 * that x must stay >= 86 to clear the weekday/day header, and LP already
 * starts at 92, leaving only ~6 px. This row is empty from the temp badge
 * (ends x=58) all the way to the right edge, so it costs nothing to place
 * here and disturbs no existing element.
 *
 * Text rather than the Bluetooth rune, for now. The rune is genuinely free to
 * draw — it is five straight segments and gfx.c already has drawLine():
 *     (cx-r, .25h) -> (cx+r, .75h) -> (cx, h) -> (cx, 0)
 *                  -> (cx+r, .25h) -> (cx-r, .75h)
 * The blocker is not flash (221 KB spare) but that the palette constants
 * (ACCENT_R/DIM_R/BACK_R) and beginFieldBand() are static inside display.c.
 * Drawing the glyph means either editing that file or duplicating the palette
 * across a module boundary. Swap this for the rune when display.c is free. */
#define CLOCK_BLE_RIGHT   (WIDTH - 2)
#define CLOCK_BLE_WIDTH   16

/* Wear indicator, stacked directly above the Bluetooth badge in the same
 * right-hand column.
 *
 * 50 = CLOCK_SOC_TEMP_Y_MARGIN (34) + FONT_SMALL height (12) + the 2+2 px of
 * padding printStatusField()/printWearField() put around their boxes. That is
 * exactly flush: this badge's padded box ends where the BT badge's begins, so
 * the two never overlap and neither can clear part of the other. Do not shrink
 * it below 50 — the boxes would then fight over the same rows, and because
 * each one clears before it draws, the visible result would depend on which
 * dirty flag happened to be serviced last.
 *
 * The band from the big time display (ends ~y=74) down to the SoC-temp row is
 * otherwise empty, so this disturbs nothing.
 *
 * Same width as the BT badge so the column edges line up. */
#define CLOCK_WEAR_Y_MARGIN 50
#define CLOCK_WEAR_RIGHT    CLOCK_BLE_RIGHT
#define CLOCK_WEAR_WIDTH    CLOCK_BLE_WIDTH

/* Power-status indicator row, top-right, tucked between the battery badge
 * (y 4..16) and the big time display (starts at CLOCK_TIME_Y = line 1 of
 * FONT_XXLARGE = y 46). x >= 86 keeps it clear of the weekday/day header,
 * which is drawn from x=10 in FONT_LARGE and runs to roughly x=70 at its
 * longest ("Wed 28"). Both indicators are constant text whose *state* is
 * carried by color (printStatusField: accent = on, dim = off), so the badge
 * width never changes and the two boxes can sit tight against each other. */
#define CLOCK_STATUS_Y        22
#define CLOCK_STATUS_FONT     FONT_SMALL
#define CLOCK_CHG_RIGHT       (WIDTH - 2)
#define CLOCK_CHG_WIDTH       14   /* "CX" at FONT_SMALL = 12 px */
#define CLOCK_LOWPWR_RIGHT    (CLOCK_CHG_RIGHT - CLOCK_CHG_WIDTH - 4)
#define CLOCK_LOWPWR_WIDTH    16   /* "LP" at FONT_SMALL = 12 px */

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


/* Date-setting screen. Deliberately mirrors the invert-branch of
 * display_out_time() above (same line, x and font) so the TIME and DATE
 * screens of system_prompt_for_time_UI_FUNC() line up with each other as the
 * user pages between them. */
#define DATE_SET_LINE 2
#define DATE_SET_X    20
#define DATE_SET_FONT FONT_SMALL

void display_out_date(Date date, date_invert_field_t invertField) {
    char date_str[16];
    sprintf(date_str, "%02u/%02u/%04u", date.month, date.day, date.year);

    // Date format: "MM/DD/YYYY"
    //   Month: indices 0-1
    //   Day:   indices 3-4
    //   Year:  indices 6-9
    int invertStart = -1;
    int invertEnd = -1;

    switch (invertField) {
        case DATE_INVERT_MONTH:
            invertStart = 0;
            invertEnd = 1;
            break;
        case DATE_INVERT_DAY:
            invertStart = 3;
            invertEnd = 4;
            break;
        case DATE_INVERT_YEAR:
            invertStart = 6;
            invertEnd = 9;
            break;
        default:
            break;
    }

    printLineWithInversion(date_str, DATE_SET_LINE, DATE_SET_X, DATE_SET_FONT, invertStart, invertEnd);
}


void display_out_pedometer(int steps) {
    char text[15];
    sprintf(text, "%d", steps);
    printFieldRightAligned(text, HEIGHT - CLOCK_STEPS_Y_MARGIN, WIDTH - 2,
                            CLOCK_BADGE_WIDTH, CLOCK_BADGE_FONT);
}


void display_out_temp(float temp) {
    char text[12];
    /* "I:" = IMU die. imu_get_temp() (imu.c) already converts the raw Celsius
     * register value to Fahrenheit before returning it — this was mislabeled
     * "C" once, so keep the unit visible here rather than relying on the
     * badge above to imply it. */
    sprintf(text, "I:%.1f F", (double)temp);
    printFieldLeftAligned(text, HEIGHT - CLOCK_STEPS_Y_MARGIN, CLOCK_TEMP_X,
                           CLOCK_TEMP_FIELD_W, CLOCK_BADGE_FONT);
}


void display_out_soc_temp(float temp) {
    char text[12];
    /* "M:" = MCU die. Both temperature badges are labelled and both carry the
     * unit, so neither depends on the other being on screen to be read. */
    sprintf(text, "M:%.1f F", (double)temp);
    printFieldLeftAligned(text, HEIGHT - CLOCK_SOC_TEMP_Y_MARGIN, CLOCK_TEMP_X,
                           CLOCK_TEMP_FIELD_W, CLOCK_BADGE_FONT);
}



/* ---- Activity screen ------------------------------------------------------
 *
 * A list, because the activity catalogue is meant to grow (eating, driving,
 * phone, TV, working...). It reads the catalogue from activity.c rather than
 * taking the rows as parameters — a variable-length list is awkward to pass,
 * and the alternative is every caller re-deriving the same table.
 *
 * The running session's elapsed time gets its own bottom row rather than
 * being appended to its list row: at FONT_MEDIUM the screen fits ~16
 * characters, and "> Running  1:23:45" does not, so inlining it would clip
 * silently the moment a session ran past an hour.
 */
#define ACT_FONT          FONT_MEDIUM
#define ACT_X             4
#define ACT_ROWS_PER_PAGE 5     /* rows 1..5; row 0 is the title, row 6 status */
#define ACT_STATUS_ROW    6
#define ACT_ROW_MAX       20    /* "> Running        *" + NUL */

static void act_fmt_elapsed(char *out, size_t out_len, uint32_t sec)
{
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    uint32_t s = sec % 60;

    if (h > 0) {
        snprintf(out, out_len, "%u:%02u:%02u", h, m, s);
    } else {
        snprintf(out, out_len, "%02u:%02u", m, s);
    }
}

void display_out_activity(size_t cursor, bool full_redraw)
{
    static char last_row[ACT_ROWS_PER_PAGE][ACT_ROW_MAX];
    static char last_status[ACT_ROW_MAX];

    size_t count = activity_count();
    size_t page  = cursor / ACT_ROWS_PER_PAGE;
    size_t start = page * ACT_ROWS_PER_PAGE;

    if (full_redraw) {
        clear_display();
        printLine("ACTIVITY", 0, ACT_X, ACT_FONT);

        for (int i = 0; i < ACT_ROWS_PER_PAGE; i++) {
            last_row[i][0] = '\0';
        }
        last_status[0] = '\0';
    }

    for (size_t i = 0; i < ACT_ROWS_PER_PAGE; i++) {
        size_t idx = start + i;
        char   row[ACT_ROW_MAX];

        if (idx < count) {
            activity_id_t id = activity_id_at(idx);

            /* "*" marks the running session so the list alone answers "am I
             * tracking anything right now" without reading the status row. */
            snprintf(row, sizeof(row), "%c%s%s",
                     idx == cursor ? '>' : ' ',
                     activity_name_at(idx),
                     (activity_is_active() && activity_current() == id) ? " *" : "");
        } else {
            row[0] = '\0';   /* blank the unused rows on the last page */
        }

        if (strcmp(row, last_row[i]) != 0) {
            clearAndPrintLine(row, i + 1, ACT_X, ACT_FONT);
            strncpy(last_row[i], row, ACT_ROW_MAX - 1);
            last_row[i][ACT_ROW_MAX - 1] = '\0';
        }
    }

    char status[ACT_ROW_MAX];

    if (activity_is_active()) {
        char elapsed[16];
        act_fmt_elapsed(elapsed, sizeof(elapsed), activity_elapsed_sec());
        snprintf(status, sizeof(status), "ON %s", elapsed);
    } else {
        snprintf(status, sizeof(status), "-- idle --");
    }

    if (strcmp(status, last_status) != 0) {
        clearAndPrintLine(status, ACT_STATUS_ROW, ACT_X, ACT_FONT);
        strncpy(last_status, status, ACT_ROW_MAX - 1);
        last_status[ACT_ROW_MAX - 1] = '\0';
    }
}


void display_out_ble_indicator(int on) {
    /* Same accent/dim convention as LP and CX: the label is constant and the
     * STATE is carried by colour, so the badge never changes width and cannot
     * leave stale pixels behind. */
    printStatusField("BT", HEIGHT - CLOCK_SOC_TEMP_Y_MARGIN, CLOCK_BLE_RIGHT,
                     CLOCK_BLE_WIDTH, CLOCK_STATUS_FONT, on != 0);
}


/*
 * Unlike every other badge on this face, the wear state is carried by hue and
 * shape rather than by a label whose color merely dims. "Worn" and "not worn"
 * are equally valid steady states — there is no on/off asymmetry for an
 * accent/dim pair to express — so a dim "WR" would read as "wear detection is
 * off", which is not what it would mean. A green check and a red cross say it
 * without a legend. See printWearField() in display.c for why the drawing
 * lives there.
 *
 * Note this reports imu_is_worn() directly, so a board whose IMU never came up
 * shows the red cross: "not worn" and "cannot tell" look the same. That is the
 * same convention the CX badge already uses for its unwired charge signal.
 */
void display_out_wear_indicator(int worn) {
    printWearField(HEIGHT - CLOCK_WEAR_Y_MARGIN, CLOCK_WEAR_RIGHT,
                   CLOCK_WEAR_WIDTH, CLOCK_STATUS_FONT, worn != 0);
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
 * display_out_power_indicators: the clock face's two power-status badges.
 *
 * "LP"  — low-power mode, i.e. BOOST_SEL / the TPS63900 mode select is set to
 *         the power-save rail (power_save_is_enabled(), power.c).
 * "CX"  — charging. This board's BMS is a discrete charge-management circuit
 *         with no I2C register and no charge-status GPIO wired, so
 *         battery_charging() is stubbed false and this badge reads "not
 *         charging" permanently for now — it is on screen so the layout slot
 *         is reserved and the wiring is a one-line change once there is a
 *         real signal to read (see power.c battery_charging()). Labelled "CX"
 *         rather than "CHG" precisely because the feature is not live yet;
 *         rename it back once battery_charging() reads a real signal.
 *
 * Both are always drawn (never blanked out) so their positions stay stable;
 * inactive is dim, active is accent-colored.
 */
void display_out_power_indicators(int charging, int low_power) {
    printStatusField("LP", CLOCK_STATUS_Y, CLOCK_LOWPWR_RIGHT,
                     CLOCK_LOWPWR_WIDTH, CLOCK_STATUS_FONT, low_power != 0);
    printStatusField("CX", CLOCK_STATUS_Y, CLOCK_CHG_RIGHT,
                     CLOCK_CHG_WIDTH, CLOCK_STATUS_FONT, charging != 0);
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


/*
 * display_out_data_stats: the Data -> Log Stats screen.
 *
 * Layout is a static label column on the left and a right-aligned value
 * column, one row per stat:
 *
 *      LOG STATS
 *      Used        12.4MB
 *      Free       271.5MB
 *      Full            4%
 *      Off       13012992
 *      Seq             41
 *
 * Redraw discipline, which is the point of the rewrite: the old version did
 * clear_display() + two printLine()s on EVERY call, and it is called from
 * ui_refresh() once a second, so the title and the "Off:"/"Seq:" labels —
 * which never change — were blanked and repainted once a second along with
 * digits that hadn't moved. That's what made the screen flicker.
 *
 * Now the title and the label column are drawn once, on full_redraw only.
 * Each value gets its own fixed-width right-aligned box and is only touched
 * when its string actually differs from what was last drawn (same
 * last-drawn-value diffing as display_out_stopwatch() and the clock face).
 * On a steady screen that is zero SPI traffic per tick; while logging, only
 * the Used/Free/Full/Off rows move, and Seq only every 100 records.
 *
 * Right-aligned rather than left: these are numbers whose digit count grows
 * (12.4MB -> 121.7MB, 999 -> 1000), and printFieldRightAligned() clears its
 * whole fixed box first, so a shorter value can never leave a stale trailing
 * digit behind — the failure mode clearAndPrintLine() has with a fixed left
 * origin.
 *
 * full_redraw must be true on first entry to the screen; the caller tracks
 * that (data_stats_UI_FUNC() uses first_ui_time).
 */
#define STATS_FONT         FONT_SMALL
#define STATS_LABEL_X      4
#define STATS_VALUE_RIGHT  (WIDTH - 4)
#define STATS_VALUE_WIDTH  72

/* format_bytes: human-readable byte count into buf, e.g. "947KB", "12.4MB",
 * "1.21GB". One decimal place below 100 units and none above, so the string
 * never outgrows the value box. Integer math throughout — this runs on the
 * display path and the app already pays for one softfloat printf on the temp
 * screen; no reason to add another. */
static void format_bytes(uint64_t bytes, char *buf, size_t buflen)
{
    static const char *const units[] = { "B", "KB", "MB", "GB" };
    unsigned unit = 0;
    uint64_t scaled = bytes;
    uint64_t remainder = 0;

    while (scaled >= 1024 && unit < 3) {
        remainder = scaled % 1024;
        scaled /= 1024;
        unit++;
    }

    if (unit == 0) {
        snprintf(buf, buflen, "%u%s", (unsigned)scaled, units[unit]);
        return;
    }

    if (scaled < 100) {
        /* One decimal: tenths of the current unit, from the remainder we
         * divided away on the last step. */
        unsigned tenths = (unsigned)((remainder * 10) / 1024);
        snprintf(buf, buflen, "%u.%u%s", (unsigned)scaled, tenths, units[unit]);
        return;
    }

    snprintf(buf, buflen, "%u%s", (unsigned)scaled, units[unit]);
}

/* Draws one value into its box only if the text changed since last time.
 * last must be a caller-owned buffer of at least STATS_VAL_MAX bytes. */
#define STATS_VAL_MAX 16

static void printStatValueIfChanged(const char *text, char *last, uint32_t posY)
{
    if (strncmp(text, last, STATS_VAL_MAX) == 0) {
        return;
    }

    printFieldRightAligned(text, posY, STATS_VALUE_RIGHT, STATS_VALUE_WIDTH, STATS_FONT);
    strncpy(last, text, STATS_VAL_MAX - 1);
    last[STATS_VAL_MAX - 1] = '\0';
}

void display_out_data_stats(uint64_t used_bytes, uint64_t capacity_bytes,
                            uint32_t meta_seq, bool full_redraw)
{
    /* Last-drawn value strings, one per row. Emptied on full_redraw so every
     * row repaints once after the screen is cleared. */
    static char last_used[STATS_VAL_MAX];
    static char last_free[STATS_VAL_MAX];
    static char last_full[STATS_VAL_MAX];
    static char last_off[STATS_VAL_MAX];
    static char last_seq[STATS_VAL_MAX];

    char used_str[STATS_VAL_MAX];
    char free_str[STATS_VAL_MAX];
    char full_str[STATS_VAL_MAX];
    char off_str[STATS_VAL_MAX];
    char seq_str[STATS_VAL_MAX];

    if (full_redraw) {
        clear_display();
        printLine("LOG STATS", 0, STATS_LABEL_X, STATS_FONT);
        printLine("Used", 1, STATS_LABEL_X, STATS_FONT);
        printLine("Free", 2, STATS_LABEL_X, STATS_FONT);
        printLine("Full", 3, STATS_LABEL_X, STATS_FONT);
        printLine("Off",  4, STATS_LABEL_X, STATS_FONT);
        printLine("Seq",  5, STATS_LABEL_X, STATS_FONT);

        last_used[0] = last_free[0] = last_full[0] = '\0';
        last_off[0]  = last_seq[0]  = '\0';
    }

    /* capacity_bytes is 0 if nvs_init() never ran; don't divide by it, and
     * don't claim a free figure we can't compute. */
    uint64_t free_bytes = (capacity_bytes > used_bytes) ? capacity_bytes - used_bytes : 0;

    format_bytes(used_bytes, used_str, sizeof(used_str));

    if (capacity_bytes == 0) {
        snprintf(free_str, sizeof(free_str), "?");
        snprintf(full_str, sizeof(full_str), "?");
    }
    else {
        format_bytes(free_bytes, free_str, sizeof(free_str));
        /* Tenths of a percent: at 285 MB capacity a whole percent is ~2.8 MB,
         * so an integer percent would read 0% through the first few hours of
         * logging and hide exactly the progress this screen exists to show. */
        uint32_t tenths = (uint32_t)((used_bytes * 1000ULL) / capacity_bytes);
        snprintf(full_str, sizeof(full_str), "%u.%u%%", tenths / 10, tenths % 10);
    }

    snprintf(off_str, sizeof(off_str), "%u", (unsigned)used_bytes);
    snprintf(seq_str, sizeof(seq_str), "%u", meta_seq);

    printStatValueIfChanged(used_str, last_used, calculateLineY(1, STATS_FONT));
    printStatValueIfChanged(free_str, last_free, calculateLineY(2, STATS_FONT));
    printStatValueIfChanged(full_str, last_full, calculateLineY(3, STATS_FONT));
    printStatValueIfChanged(off_str,  last_off,  calculateLineY(4, STATS_FONT));
    printStatValueIfChanged(seq_str,  last_seq,  calculateLineY(5, STATS_FONT));
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
    /* "Fault: [" + int + "]" — 9 chars of literal plus up to 11 for a full
     * negative int32. char[12] + sprintf() was a stack smash waiting for a
     * 3-digit fault code (13 bytes incl. NUL into a 12-byte buffer); nothing
     * calls ui_fault() today, which is the only reason it never fired. */
    char text[24];
    snprintf(text, sizeof(text), "Fault: [%d]", error_code);

    clear_display();
    printLine(text, 2, 12, FONT_LARGE);  // print text displaying what kind of measurement it is
    return;
}
















/* ---------------------------------------------------------------------------
 * Temperature list + explicit "nothing to show" notice
 * ------------------------------------------------------------------------- */

/* FONT_SMALL pitch is 15 px (12 px glyph + 30% spacing) on a 10 px top margin,
 * so line 9 sits at y=145 and ends at 157 -- the last one that fits in 160.
 * Line 0 is the title, line 1 the column header, leaving 8 sample rows. */
#define TEMP_LIST_FONT      FONT_SMALL
#define TEMP_LIST_X         4
#define TEMP_LIST_FIRST_ROW 2
#define TEMP_LIST_LAST_ROW  9

/* 21 chars fit across 128 px at FONT_SMALL (6 px/char) and there is no wrap or
 * warning if that is exceeded -- text is silently clipped. The rows below are
 * 14 chars, the header 13. */
#define TEMP_LIST_ROW_MAX 24

/*
 * display_out_notice: a screen that says, in words, that there is nothing to
 * draw and why.
 *
 * Exists because the failure looked like data. Both temperature screens used
 * to fall back to display_out_measurement("Temp Graph", 0) when the history
 * ring was empty, which renders as a title and a big "0" -- indistinguishable
 * from a genuine reading of zero, and it is what sent Anders looking for a
 * broken graph when the real problem was that nothing was feeding the ring.
 * An empty state should never be able to masquerade as a measurement.
 */
void display_out_notice(const char *title, const char *line1, const char *line2)
{
    clear_display();
    printLine(title, 0, TEMP_LIST_X, FONT_MEDIUM);
    if (line1 != NULL) {
        printLine(line1, 2, TEMP_LIST_X, TEMP_LIST_FONT);
    }
    if (line2 != NULL) {
        printLine(line2, 3, TEMP_LIST_X, TEMP_LIST_FONT);
    }
}

/*
 * display_out_temp_list: the most recent temperature samples, newest at the
 * top, IMU die beside MCU die.
 *
 * Both columns on purpose: a single die temperature cannot tell self-heating
 * from sensor error, and the IMU reads several degrees warm (see
 * imu_notes.md). Side by side, the difference between the two is readable at a
 * glance, which is the entire diagnostic value.
 *
 * Redrawn wholesale rather than diffed per row. Unlike the clock face, every
 * row here SHIFTS when a new sample lands, so a per-row diff would repaint
 * almost all of them anyway; the caller already skips the redraw entirely
 * unless the ring's revision changed (once per sensor tick, ~9 s).
 */
void display_out_temp_list(const int16_t *imu_raw, const int16_t *soc_centi, size_t n)
{
    char row[TEMP_LIST_ROW_MAX];

    clear_display();
    printLine("TEMP LOG", 0, TEMP_LIST_X, FONT_MEDIUM);
    printLine(" #   IMU   MCU", 1, TEMP_LIST_X, TEMP_LIST_FONT);

    size_t rows = TEMP_LIST_LAST_ROW - TEMP_LIST_FIRST_ROW + 1;
    if (n < rows) {
        rows = n;
    }

    for (size_t i = 0; i < rows; i++) {
        /* %5.1f keeps the columns aligned up to "999.9"; the IMU sensor's
         * range cannot reach four digits, so this cannot silently shift. */
        snprintf(row, sizeof(row), "%2u %5.1f %5.1f",
                 (unsigned)(i + 1),
                 (double)imu_raw_to_fahrenheit(imu_raw[i]),
                 (double)soc_temp_centi_c_to_f(soc_centi[i]));
        printLine(row, TEMP_LIST_FIRST_ROW + i, TEMP_LIST_X, TEMP_LIST_FONT);
    }
}


/*
 * display_out_erase_confirm: the destructive-action confirmation screen.
 *
 * Layout is a safety feature, not decoration:
 *
 *  - The cursor STARTS on Cancel and Cancel is listed FIRST, so the very first
 *    SELECT after opening this screen is always harmless. Committing requires
 *    a different button (UP/DOWN) before SELECT, which means no repeated or
 *    bouncing press on one button can ever erase anything. Two presses of
 *    SELECT = open, then cancel.
 *  - It states the SIZE of what is about to be destroyed. A mis-triggered
 *    confirm then looks obviously wrong before the user commits, rather than
 *    being an unlabelled yes/no.
 *  - It names the rate settings explicitly, because losing those is the
 *    genuinely surprising part -- they live on the MT29F's CONFIG blocks and a
 *    chip erase takes them with it.
 */
void display_out_erase_confirm(uint64_t used_bytes, int cursor, bool full_redraw)
{
    char used_str[STATS_VAL_MAX];
    char line[TEMP_LIST_ROW_MAX];

    if (full_redraw) {
        clear_display();
        printLine("ERASE FLASH", 0, TEMP_LIST_X, FONT_MEDIUM);

        format_bytes(used_bytes, used_str, sizeof(used_str));
        snprintf(line, sizeof(line), "Deletes %s", used_str);
        printLine(line, 2, TEMP_LIST_X, TEMP_LIST_FONT);
        printLine("of logged data", 3, TEMP_LIST_X, TEMP_LIST_FONT);
        printLine("+ rate settings.", 4, TEMP_LIST_X, TEMP_LIST_FONT);
        printLine("Cannot be undone.", 5, TEMP_LIST_X, TEMP_LIST_FONT);
    }

    /* Only the two option rows change as the cursor moves, so they are the
     * only thing repainted on a cursor step. clearAndPrintLine() wipes to the
     * right edge, which is what removes the previous ">" marker. */
    snprintf(line, sizeof(line), "%s Cancel", cursor == 0 ? ">" : " ");
    clearAndPrintLine(line, 7, TEMP_LIST_X, TEMP_LIST_FONT);

    snprintf(line, sizeof(line), "%s ERASE ALL", cursor == 1 ? ">" : " ");
    clearAndPrintLine(line, 8, TEMP_LIST_X, TEMP_LIST_FONT);
}
