//*****************************************************************************
//! @file low_power.c
//! @brief Low-power mode policy — see low_power.h for what it does and why
//!        it deliberately does NOT touch BOOST_SEL.
//*****************************************************************************

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <power/low_power.h>
#include <st7735s.h>
#include <st7735s_compat.h>

LOG_MODULE_REGISTER(low_power, CONFIG_LOG_DEFAULT_LEVEL);

static bool user_on;
static bool batt_on;

/* Brightness in effect before the mode capped it, so releasing the mode gives
 * the user back what they chose rather than leaving them stuck at the cap. */
static uint8_t saved_backlight_pct;
static bool    backlight_capped;

static bool effective(void)
{
    return user_on || batt_on;
}

/* Apply (or release) the backlight cap to match the current effective state. */
static void apply_backlight(void)
{
    if (effective()) {
        if (!backlight_capped && backlight_pct > LOW_POWER_BACKLIGHT_MAX_PCT) {
            saved_backlight_pct = backlight_pct;
            backlight_capped    = true;
            Backlight_Pct(LOW_POWER_BACKLIGHT_MAX_PCT);
        }
    } else if (backlight_capped) {
        backlight_capped = false;
        Backlight_Pct(saved_backlight_pct);
    }
}

void low_power_init(void)
{
    user_on          = false;
    batt_on          = false;
    backlight_capped = false;
}

bool low_power_is_active(void)      { return effective(); }
bool low_power_user_enabled(void)   { return user_on; }
bool low_power_battery_engaged(void){ return batt_on; }

void low_power_set_user(bool on)
{
    if (user_on == on) {
        return;
    }
    user_on = on;
    LOG_INF("low power: user setting %s (effective=%d)",
            on ? "ON" : "OFF", (int)effective());
    apply_backlight();
}

void low_power_battery_update(int mv)
{
    /* See the header: a failed reading must not look like a flat battery. */
    if (mv <= 0) {
        return;
    }

    bool was = effective();

    if (!batt_on && mv < LOW_POWER_ENTER_MV) {
        batt_on = true;
        LOG_INF("low power: ENGAGED by battery (%d mV < %d mV)",
                mv, LOW_POWER_ENTER_MV);
    } else if (batt_on && mv > LOW_POWER_EXIT_MV) {
        batt_on = false;
        LOG_INF("low power: released by battery (%d mV > %d mV)",
                mv, LOW_POWER_EXIT_MV);
    }

    if (effective() != was) {
        apply_backlight();
    }
}

uint8_t low_power_cap_backlight(uint8_t requested_pct)
{
    if (effective() && requested_pct > LOW_POWER_BACKLIGHT_MAX_PCT) {
        return LOW_POWER_BACKLIGHT_MAX_PCT;
    }
    return requested_pct;
}
