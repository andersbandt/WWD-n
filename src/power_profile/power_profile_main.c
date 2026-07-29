//*****************************************************************************
//!
//! @file power_profile_main.c
//! @brief Power-profiling harness — replaces src/main.c when built with
//!        -DPOWER_PROFILE_BUILD=ON (see root CMakeLists.txt).
//!
//! Runs a fixed, scripted sequence of hardware states (display backlight
//! sweep, battery-divider held-on, IMU ODR sweep, NVS writing, DC/DC vs LDO)
//! each held for a known dwell time, so external PS/DMM current logging can
//! be sliced by elapsed time after the fact rather than needing any live
//! signaling from the device. See debug/power_profile/README.md for the
//! measurement/execution side of this.
//!
//! Deliberately does NOT call usb_enable() — the board must be powered via
//! the bench PS (not direct USB) for a clean current measurement, and an
//! active USB connection would add its own confounding current draw. All
//! logging goes out over SEGGER RTT only (CONFIG_LOG_BACKEND_RTT=y is
//! already set in prj.conf), purely as an optional audit trail — timing for
//! analysis comes from the schedule below, not from anything read live.
//!
//*****************************************************************************

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>

#include <display.h>
#include <st7735s.h>
#include <imu.h>
#include <imu_bringup.h>
#include <nvs.h>
#include <power/power.h>
#include <peripheral/clock.h>

LOG_MODULE_REGISTER(power_profile, LOG_LEVEL_INF);

/* ---------------------------------------------------------------------------
 * Schedule — keep this in sync with debug/power_profile/schedule.json.
 * Each state is held for DWELL_MS; the analysis script discards a settle
 * margin at the start of each slice (see debug/power_profile/README.md) and
 * averages the remainder. Total run time = number of states * DWELL_MS.
 * ------------------------------------------------------------------------- */
#define DWELL_MS 8000

typedef void (*state_setup_fn)(void);

struct power_state {
    const char *name;
    state_setup_fn setup;
};

/* ---- per-state setup functions ---- */

static void st_dcdc_on(void)
{
    nrf_power_dcdcen_set(NRF_POWER, true);
}

static void st_dcdc_off(void)
{
    nrf_power_dcdcen_set(NRF_POWER, false);
}

static void st_display_off(void)
{
    if (display_status) {
        Backlight_Pct(0);
        switch_display(false);
    }
}

static void st_display_bl(uint8_t pct)
{
    if (display_status) {
        switch_display(true);
        Backlight_Pct(pct);
    }
}
static void st_display_bl_0(void)   { st_display_bl(0); }
static void st_display_bl_25(void)  { st_display_bl(25); }
static void st_display_bl_50(void)  { st_display_bl(50); }
static void st_display_bl_75(void)  { st_display_bl(75); }
static void st_display_bl_100(void) { st_display_bl(100); }

static void st_batt_div_off(void)
{
    power_debug_hold_vbat_div(false);
}

static void st_batt_div_on(void)
{
    power_debug_hold_vbat_div(true);
}

static void st_imu_odr(uint16_t hz)
{
    if (imu_alive) {
        imu_set_odr(hz);
    }
}
static void st_imu_odr_25(void)  { st_imu_odr(25); }
static void st_imu_odr_50(void)  { st_imu_odr(50); }
static void st_imu_odr_100(void) { st_imu_odr(100); }
static void st_imu_odr_200(void) { st_imu_odr(200); }
static void st_imu_odr_400(void) { st_imu_odr(400); }
static void st_imu_odr_800(void) { st_imu_odr(800); }

static bool nvs_alive;

static void st_nvs_idle(void)
{
    /* nvs_init() already ran once at boot; this state just means "NVS is up
     * but not being written to" — nothing to do here. */
}

static void st_nvs_writing(void)
{
    /* Write for the whole dwell at a fixed ~20 records/sec, a NVS_LOG_IMU_SAMPLES-
     * shaped payload isn't needed here — a plain temperature record exercises
     * the same mt29f_bus_mutex/nvs_state_mutex + NAND page-write path. */
    if (!nvs_alive) {
        return;
    }
    for (int i = 0; i < (DWELL_MS / 50); i++) {
        struct record_temperature t = { .raw = (int16_t)i };
        nvs_log_record(RECORD_TEMPERATURE, &t, sizeof(t), get_dt_ticks());
        k_msleep(50);
    }
}

static void st_combo_realistic(void)
{
    /* Rough approximation of real operation: display on at a middle
     * brightness, IMU streaming, NVS writing — all together, as a sanity
     * spot-check against the OFAT deltas above, not a systematic sweep. */
    st_display_bl(50);
    st_imu_odr(100);
    st_nvs_writing();  /* this one call already blocks for the full dwell */
}

/* ---------------------------------------------------------------------------
 * NOTE on st_combo_realistic: it calls st_nvs_writing() which itself blocks
 * for DWELL_MS via its own k_msleep loop, so the outer per-state k_msleep in
 * run_sequence() below would double the dwell for that one state. Handled by
 * giving that state DWELL_MS 0 in the table (see combo entry) — the writing
 * loop itself is the dwell.
 * ------------------------------------------------------------------------- */

static const struct power_state states[] = {
    { "floor_dcdc_on",     st_dcdc_on },
    { "floor_dcdc_off",    st_dcdc_off },
    { "dcdc_on_restore",   st_dcdc_on },   /* back to default for the rest */

    { "display_bl_0",      st_display_bl_0 },
    { "display_bl_25",     st_display_bl_25 },
    { "display_bl_50",     st_display_bl_50 },
    { "display_bl_75",     st_display_bl_75 },
    { "display_bl_100",    st_display_bl_100 },
    { "display_off",       st_display_off },

    { "batt_div_off",      st_batt_div_off },
    { "batt_div_on",       st_batt_div_on },
    { "batt_div_off_end",  st_batt_div_off },

    { "imu_odr_25",        st_imu_odr_25 },
    { "imu_odr_50",        st_imu_odr_50 },
    { "imu_odr_100",       st_imu_odr_100 },
    { "imu_odr_200",       st_imu_odr_200 },
    { "imu_odr_400",       st_imu_odr_400 },
    { "imu_odr_800",       st_imu_odr_800 },

    { "nvs_idle",          st_nvs_idle },
    { "nvs_writing",       st_nvs_writing },  /* blocks for its own dwell */

    { "combo_realistic",   st_combo_realistic },  /* blocks for its own dwell */
};

#define NUM_STATES (sizeof(states) / sizeof(states[0]))

/* States whose setup function already blocks for the full dwell itself
 * (st_nvs_writing, st_combo_realistic) — run_sequence() must not also sleep
 * DWELL_MS after calling these, or they'd take 2x as long as scheduled. */
static bool state_self_dwells(state_setup_fn fn)
{
    return fn == st_nvs_writing || fn == st_combo_realistic;
}

static void run_sequence(void)
{
    int64_t t0 = k_uptime_get();

    for (size_t i = 0; i < NUM_STATES; i++) {
        int64_t elapsed = k_uptime_get() - t0;

        LOG_INF("[power_profile] t=%lldms state=%zu/%zu name=%s",
                elapsed, i + 1, NUM_STATES, states[i].name);

        states[i].setup();

        if (!state_self_dwells(states[i].setup)) {
            k_msleep(DWELL_MS);
        }
    }

    LOG_INF("[power_profile] sequence complete, t=%lldms total",
            k_uptime_get() - t0);
}

int main(void)
{
    LOG_INF("=== WWD-n power profiling harness ===");
    LOG_INF("Board must be PS-powered (not direct USB) for a clean measurement.");
    LOG_INF("%zu states, %d ms dwell each (except self-timed states) -> see schedule",
            NUM_STATES, DWELL_MS);

    /* One-time init of everything the sequence touches, so init transients
     * (SPI/I2C probing, panel reset pulse, etc.) happen before the timed
     * sequence starts rather than polluting the first state's measurement. */
    power_init();

    init_display();
    if (!display_status) {
        LOG_WRN("init_display() failed — display states will be no-ops");
    } else {
        clear_display();
    }

    {
        int rc = imu_init();
        imu_alive = (rc == 0);
        if (!imu_alive) {
            LOG_WRN("imu_init() failed (%d) — IMU states will be no-ops", rc);
        }
    }

    nvs_init();
    nvs_alive = nvs_ready();
    if (!nvs_alive) {
        LOG_WRN("nvs_init() failed — NVS states will be no-ops");
    }

    /* Let everything settle before the timed sequence starts. */
    k_msleep(2000);

    LOG_INF("[power_profile] BEGIN t=0");
    run_sequence();

    /* Leave hardware in a quiet state afterward. */
    st_display_off();
    power_debug_hold_vbat_div(false);
    nrf_power_dcdcen_set(NRF_POWER, true);

    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
