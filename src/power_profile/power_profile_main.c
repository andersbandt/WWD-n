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
#include <ICM_42670.h>
#include <imu_bringup.h>
#include <nvs.h>
#include <power/power.h>
#include <peripheral/clock.h>
#ifdef PP_BLE
#include <ble/ble.h>
#endif

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

/* BOOST_SEL / TPS63900 mode select (MCP23008 GP6).
 *
 * NB the pin is named by its LOGICAL level here, not by an assumed output
 * voltage. power.c documents active-HIGH as the LOWER VCC (power-save) rail,
 * but that has been documented backwards before, so these states are named
 * boost_sel_0 / boost_sel_1 after what is actually driven. Measure VCC to
 * decide which is 2.7 V and which is 3.0 V -- do not infer it from the name.
 *
 * Display is forced off first so the delta is not buried under ~4.8 mA of
 * panel + backlight. */
static void st_display_off(void);

static void st_boost_sel_0(void)
{
    st_display_off();
    power_save_enable(false);
}

static void st_boost_sel_1(void)
{
    st_display_off();
    power_save_enable(true);
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

    /* Appended 2026-08-24 -- deliberately at the END so every pre-existing
     * state keeps its original start/end time and earlier runs stay
     * comparable. Item 3: TPS63900 VCC select. */
    { "boost_sel_0",       st_boost_sel_0 },
    { "boost_sel_1",       st_boost_sel_1 },
};

#define NUM_STATES (sizeof(states) / sizeof(states[0]))

static void run_sequence(void)
{
    int64_t t0 = k_uptime_get();

    for (size_t i = 0; i < NUM_STATES; i++) {
        int64_t elapsed = k_uptime_get() - t0;

        LOG_INF("[power_profile] t=%lldms state=%zu/%zu name=%s",
                elapsed, i + 1, NUM_STATES, states[i].name);

        /* Pad every state to exactly DWELL_MS regardless of how long its
         * setup blocked for.
         *
         * The old form skipped the sleep entirely for "self-dwelling" states
         * (st_nvs_writing, st_combo_realistic) on the assumption that their
         * write loops take the full dwell. That assumption breaks whenever
         * NVS is not up -- on SN1 (wrong NAND) both return immediately, so
         * the two states lasted ~0 ms instead of 8 s and the whole tail of
         * the run happened ~16 s earlier than schedule.json says. Every
         * schedule-sliced number from nvs_idle onward was then garbage, which
         * is exactly the kind of silent misalignment that is hard to spot in
         * an averaged table. Measuring the actual elapsed time and sleeping
         * only the remainder is correct in both cases and needs no special
         * casing. */
        int64_t state_start = k_uptime_get();

        states[i].setup();

        int64_t used = k_uptime_get() - state_start;
        if (used < DWELL_MS) {
            k_msleep((int32_t)(DWELL_MS - used));
        } else if (used > DWELL_MS + 100) {
            LOG_WRN("[power_profile] state %s overran its dwell: %lldms > %dms",
                    states[i].name, used, DWELL_MS);
        }
    }

    LOG_INF("[power_profile] sequence complete, t=%lldms total",
            k_uptime_get() - t0);
}

#ifdef PP_TEARDOWN
/* ---------------------------------------------------------------------------
 * Teardown / attribution mode (-DPOWER_PROFILE_TEARDOWN=ON).
 *
 * The 21-state sequence measures what each subsystem costs while ACTIVE. It
 * cannot say what owns the ~2.6 mA idle floor, because every driver is already
 * initialised before state 0 runs. This mode inverts that: start with NOTHING
 * initialised and bring exactly one subsystem up per dwell, so the current
 * step between consecutive states is that subsystem's cost.
 *
 * Additive rather than subtractive on purpose -- "disabling" a peripheral
 * rarely returns it to its true pre-init state (the ICM-42670 and MT29F have
 * no power-down API here at all), so subtracting would under-report.
 *
 * Deltas remain valid with a debugger attached (it adds a constant offset to
 * every state); only the absolute floor is inflated by it.
 * ------------------------------------------------------------------------- */

static void st_td_nothing(void)      { /* baseline: no driver init at all */ }
static void st_td_power(void)        { power_init(); }

static void st_td_display_asleep(void)
{
    init_display();
    if (display_status) {
        /* Panel initialised but immediately put to sleep -- this is exactly
         * the state the real app's display timeout leaves it in, and the one
         * the 2.6 mA floor was measured in. */
        switch_display(false);
    }
}

static void st_td_imu(void)
{
    int rc = imu_init();
    imu_alive = (rc == 0);
}

static void st_td_nand(void)
{
    nvs_init();
    nvs_alive = nvs_ready();
}

static void st_td_imu_100hz(void)
{
    if (imu_alive) {
        imu_set_odr(100);
    }
}

static void st_td_display_on(void)
{
    if (display_status) {
        switch_display(true);
        Backlight_Pct(100);
    }
}

static const struct power_state teardown_states[] = {
    { "bare_idle",        st_td_nothing },
    { "add_power_init",   st_td_power },
    { "add_display_slp",  st_td_display_asleep },
    { "add_imu",          st_td_imu },
    { "add_nand",         st_td_nand },
    { "add_imu_100hz",    st_td_imu_100hz },
    { "display_on_bl100", st_td_display_on },
};

static void run_teardown(void)
{
    int64_t t0 = k_uptime_get();

    for (size_t i = 0; i < ARRAY_SIZE(teardown_states); i++) {
        int64_t state_start = k_uptime_get();

        LOG_INF("[teardown] t=%lldms state=%zu/%zu name=%s",
                state_start - t0, i + 1, ARRAY_SIZE(teardown_states),
                teardown_states[i].name);

        teardown_states[i].setup();

        int64_t used = k_uptime_get() - state_start;
        if (used < DWELL_MS) {
            k_msleep((int32_t)(DWELL_MS - used));
        }
    }

    LOG_INF("[teardown] complete, t=%lldms", k_uptime_get() - t0);
}
#endif /* PP_TEARDOWN */

#ifdef PP_PROBE
/* ---------------------------------------------------------------------------
 * Probe mode (-DPOWER_PROFILE_PROBE=ON) — two open questions in one image.
 *
 * (1) What owns the ~2.1 mA floor that is present with NO drivers initialised?
 *     The debugger was ruled out by measurement (attached vs detached traces are
 *     identical), so it is real. `bare_spin` is the discriminator: it busy-waits
 *     so the CPU is 100% awake. If bare_spin >> bare_idle then the idle path IS
 *     sleeping and the floor lives somewhere else; if they are close, the CPU
 *     never sleeps and that is the whole story.
 *     NB do NOT try to infer this from HFCLKSTAT over SWD — reading it requires
 *     halting the CPU, and a halted CPU is awake, so HFCLK always reads running.
 *     That reading is an artefact; only current tells the truth here.
 *
 * (2) What does the gyro cost? imu_start()/imu_set_odr() enable accel AND gyro
 *     in low-noise mode. The ICM-42670-P gyro's drive circuit runs continuously
 *     once enabled, which would explain the measured +0.41 mA that does not
 *     scale with ODR. Stepping accel-only -> +gyro isolates it.
 * ------------------------------------------------------------------------- */

static void pr_nothing(void) { }

static void pr_spin(void)
{
    /* Busy-wait the whole dwell so the CPU never reaches the idle thread. */
    int64_t end = k_uptime_get() + DWELL_MS;
    while (k_uptime_get() < end) {
        /* deliberately empty */
    }
}

static void pr_power_init(void)   { power_init(); }
static void pr_icm_init(void)     { (void)init_icm(); }
static void pr_accel_only(void)   { (void)startAccel(100, 16); }
static void pr_add_gyro(void)     { (void)startGyro(100, 2000); }

static void pr_both_odr_800(void)
{
    (void)startAccel(800, 16);
    (void)startGyro(800, 2000);
}

static void pr_periph_off(void)
{
    /* Blunt-force: disable every EasyDMA peripheral and the analog blocks.
     * Nothing runs after this state, so breaking the drivers is fine. */
    *(volatile uint32_t *)0x40002500 = 0;  /* UARTE0        */
    *(volatile uint32_t *)0x40003500 = 0;  /* SPIM0/TWIM0   */
    *(volatile uint32_t *)0x40004500 = 0;  /* SPIM1/TWIM1   */
    *(volatile uint32_t *)0x40023500 = 0;  /* SPIM2         */
    *(volatile uint32_t *)0x40007500 = 0;  /* SAADC         */
    *(volatile uint32_t *)0x4001C500 = 0;  /* PWM0          */
    *(volatile uint32_t *)0x40021500 = 0;  /* PWM1          */
    *(volatile uint32_t *)0x40022500 = 0;  /* PWM2          */
    *(volatile uint32_t *)0x40027500 = 0;  /* USBD          */
}

static const struct power_state probe_states[] = {
    { "bare_idle",       pr_nothing },
    { "bare_spin",       pr_spin },
    { "power_init",      pr_power_init },
    { "icm_init_only",   pr_icm_init },
    { "accel_only",      pr_accel_only },
    { "accel_plus_gyro", pr_add_gyro },
    { "both_odr_800",    pr_both_odr_800 },
    { "periph_off",      pr_periph_off },
};

static void run_probe(void)
{
    int64_t t0 = k_uptime_get();

    for (size_t i = 0; i < ARRAY_SIZE(probe_states); i++) {
        int64_t state_start = k_uptime_get();

        LOG_INF("[probe] t=%lldms state=%zu/%zu name=%s",
                state_start - t0, i + 1, ARRAY_SIZE(probe_states),
                probe_states[i].name);

        probe_states[i].setup();

        int64_t used = k_uptime_get() - state_start;
        if (used < DWELL_MS) {
            k_msleep((int32_t)(DWELL_MS - used));
        }
    }

    LOG_INF("[probe] complete, t=%lldms", k_uptime_get() - t0);
}
#endif /* PP_PROBE */

#ifdef PP_BLE
/* ---------------------------------------------------------------------------
 * BLE mode (-DPOWER_PROFILE_BLE=ON) — what does the radio cost?
 *
 * Two questions, and they need different measurement styles, so this mode is
 * half scripted and half host-driven.
 *
 * The SCRIPTED half answers "what does keeping the advertiser going cost?".
 * Three states decompose it, because "BLE on" is really two separate costs:
 *   ble_off        — quiet board, bt_enable() never called: the reference.
 *   ble_stack_up   — controller enabled but NOT advertising. This is exactly
 *                    what ble_set_enabled(false) leaves behind, so the step
 *                    from here to ble_adv is the true saving of the user's
 *                    BLE-off toggle, while the step from ble_off is what
 *                    compiling BLE out entirely would save.
 *   ble_adv        — advertising with BT_LE_ADV_CONN (100-150 ms interval).
 *
 * The HOLD half answers "what does data transfer cost?". A connection cannot
 * be scripted from this side — the central decides when to connect, what
 * connection interval to negotiate, and when to subscribe — so after the
 * scripted states this parks the board advertising forever and lets the host
 * drive the phases (connect / idle / notify / sustained reads) against the
 * same continuous current log. Phase boundaries come from the host's
 * timestamps, not from a schedule.
 *
 * Nothing else is initialised: no IMU (0.41 mA), no NAND, panel asleep. The
 * radio deltas here are small and would otherwise sit inside the scatter of
 * subsystems that have already been characterised.
 * ------------------------------------------------------------------------- */

#define PP_BLE_DWELL_MS 30000   /* not 8 s: see README — 8 s never settles */

static void pb_ble_off(void)
{
    /* Reference state. bt_enable() has not been called, so the radio and its
     * clocks are untouched. */
}

static void pb_stack_up(void)
{
    ble_init();
    /* ble_init() starts advertising; stop it again so this state isolates the
     * cost of the controller being up on its own. The dwell is long enough
     * that the few ms of advertising at the head is discarded with the settle
     * margin. */
    ble_set_enabled(false);
}

static void pb_adv_on(void)
{
    ble_set_enabled(true);
}

static const struct power_state ble_states[] = {
    { "ble_off",      pb_ble_off },
    { "ble_stack_up", pb_stack_up },
    { "ble_adv",      pb_adv_on },
};

static void run_ble(void)
{
    int64_t t0 = k_uptime_get();

    for (size_t i = 0; i < ARRAY_SIZE(ble_states); i++) {
        int64_t state_start = k_uptime_get();

        LOG_INF("[ble] t=%lldms state=%zu/%zu name=%s",
                state_start - t0, i + 1, ARRAY_SIZE(ble_states),
                ble_states[i].name);

        ble_states[i].setup();

        int64_t used = k_uptime_get() - state_start;
        if (used < PP_BLE_DWELL_MS) {
            k_msleep((int32_t)(PP_BLE_DWELL_MS - used));
        }
    }

    LOG_INF("[ble] scripted states complete, t=%lldms — now advertising"
            " indefinitely, host drives the connection phases",
            k_uptime_get() - t0);
}
#endif /* PP_BLE */

int main(void)
{
    LOG_INF("=== WWD-n power profiling harness ===");
    LOG_INF("Board must be PS-powered (not direct USB) for a clean measurement.");
    LOG_INF("%zu states, %d ms dwell each (padded to exactly this) -> see schedule",
            NUM_STATES, DWELL_MS);

#ifdef PP_BLE
    /* Quiet board first, then the scripted BLE states, then hold advertising
     * so the host can drive the connection phases. Deliberately no IMU and no
     * NVS init — see the comment on ble_states above. */
    power_init();
    init_display();
    if (display_status) {
        switch_display(false);
    }
    power_debug_hold_vbat_div(false);
    nrf_power_dcdcen_set(NRF_POWER, true);

    k_msleep(2000);
    LOG_INF("[ble] BEGIN t=0");
    run_ble();
    while (1) {
        k_sleep(K_FOREVER);
    }
#endif

#ifdef PP_PROBE
    /* Skip all eager init: run_probe() brings pieces up one dwell at a time. */
    k_msleep(2000);
    LOG_INF("[probe] BEGIN t=0");
    run_probe();
    while (1) {
        k_sleep(K_FOREVER);
    }
#endif

#ifdef PP_TEARDOWN
    /* Deliberately skip the eager init below -- run_teardown() brings each
     * subsystem up one dwell at a time so the deltas attribute the floor. */
    k_msleep(2000);
    LOG_INF("[teardown] BEGIN t=0");
    run_teardown();
    while (1) {
        k_sleep(K_FOREVER);
    }
#endif

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

#ifdef PP_BOOST_HOLD
    /* VBAT-sweep hold mode (-DPOWER_PROFILE_BOOST_HOLD=0|1).
     *
     * The scripted sequence is useless for a supply sweep: each state lasts
     * 8 s while a sweep takes minutes. So instead of running it, park the
     * board in one fixed, quiet state (display off, divider off, internal
     * DC/DC on) with BOOST_SEL held at a known level, and let the host step
     * VBAT underneath it. One image per BOOST_SEL level. */
    st_display_off();
    power_debug_hold_vbat_div(false);
    nrf_power_dcdcen_set(NRF_POWER, true);
    power_save_enable(PP_BOOST_HOLD ? true : false);

    LOG_INF("[power_profile] HOLD BOOST_SEL=%d (power_save_is_enabled=%d)",
            PP_BOOST_HOLD, (int)power_save_is_enabled());

    while (1) {
        k_sleep(K_FOREVER);
    }
#else
    LOG_INF("[power_profile] BEGIN t=0");
    run_sequence();

    /* Leave hardware in a quiet state afterward. */
    st_display_off();
    power_debug_hold_vbat_div(false);
    nrf_power_dcdcen_set(NRF_POWER, true);

    while (1) {
        k_sleep(K_FOREVER);
    }
#endif

    return 0;
}
