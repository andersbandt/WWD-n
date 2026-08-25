//*****************************************************************************
//!
//! @file main.c
//! @author Anders Bandt
//! @brief Main code for WWD with Nordic
//! @version 0.9
//! @date December 2025
//!
//! Bring-up harness graduating into the real application: USB CDC ACM console
//! + binary host protocol, RV-3028 RTC, ICM-42670 IMU, MT29F/NVS logging,
//! ST7735S display, and the application threads that drive the clock screen.
//! Per-module bring-up/diagnostic code has moved out to
//! peripheral/rv3028_bringup.c, hardware/ic/imu/imu_bringup.c, and
//! memory/nvs_bringup.c — this file is the boot sequence and thread wiring.
//!
//*****************************************************************************

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

/* IMU */
#include <imu.h>
#include <imu_bringup.h>
#include <ICM_42670.h>   /* getTempDataFromIMUReg() for the BLE status feed */
#include <display.h>

/* NVS bring-up phase + pipeline tick */
#include <memory/nvs_bringup.h>

#include "peripheral/clock.h"   /* get_current_time() */
#include "peripheral/rv3028.h"
#include "peripheral/rv3028_bringup.h"
#include "peripheral/soc_temp.h"
#include <activity/activity.h>
#include <ble/ble.h>
#include "peripheral/interrupt.h"
#include "peripheral/timer.h"
#include "hardware/led.h"
#include "hardware/button.h"
#include "power/power.h"
#include <ui.h>
#include <power/low_power.h>

/* Host command protocol (cdc_acm_uart1) */
#include "comm/protocol.h"
#include "comm/rate_config.h"

#include <util/cdc_debug.h>

#include "main.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *const cdc_dev =
    DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

static const struct device *const i2c_dev =
    DEVICE_DT_GET(DT_NODELABEL(i2c0));

/* First real display bring-up: init_display() drives the panel through
 * SPI1@2 (CS P0.28) + DC (P0.29) + RESET (MCP23008 GP7). Run before
 * nvs_bringup_phase() so a working/broken result here is judged before any NAND
 * traffic hits the shared SPI1 bus — see imu_notes.md / nvs_notes.md on
 * that shared-bus history. */
static void display_phase(void)
{
    cdc_write("\r\n===== ST7735S display phase =====\r\n");

    init_display();
    cdc_printf("  init_display: %s\r\n", display_status ? "OK" : "FAILED");

    if (display_status) {
        clear_display();
        printLine("WWD-n", 0, 10, FONT_LARGE);
        printLine("display bring-up", 2, 10, FONT_SMALL);
    }

    cdc_write("==================================\r\n");
}

/* ---------------------------------------------------------------------------
 * Application threads — see CLAUDE.md "Threading Model". Four threads,
 * semaphore-driven off timer.c's periodic ticks and interrupt.c's GPIO
 * callbacks, replacing the manual polling loop used during bring-up.
 * ------------------------------------------------------------------------- */

/* 2048 rather than 1024: both threads now make a real RV-3028 I2C read
 * (get_current_time() -> rv3028_get_time() -> Zephyr rtc_get_time() ->
 * i2c_transfer, and as of 2026-08-22 get_date() -> rv3028_get_date() the same
 * way) or IMU SPI reads, several calls deep, under this project's
 * CONFIG_NO_OPTIMIZATIONS build. 1024B undersized display_timeout_thread's
 * far simpler body enough to overflow into a CPU-lockup reset (see
 * project_thread_bringup_crash memory) — these threads do considerably more
 * per tick and had never actually been runtime-exercised with this real
 * logic before, so don't assume 1024B was ever validated for it. */
/* 2048 -> 4096 on 2026-08-24, during the -Os production-build changeover.
 * Static worst-case call-depth analysis (-fstack-usage frames walked over the
 * objdump call graph — see debug/stack_budget.md) put this thread at 1976 B of
 * its 2048 B stack under the OLD NO_OPTIMIZATIONS build: 96.5% used, ~72 B of
 * margin, via
 *   sensor_update_thread_entry -> imu_get_pedo -> getPedometer
 *     -> inv_imu_apex_get_data_activity -> inv_imu_read_reg
 *     -> read_mclk_reg -> inv_imu_switch_on_mclk
 * That is the 9 s pedometer tick — a path that runs constantly, so this was
 * live the whole time and simply never quite tipped over.
 * At -Os the same chain is 288 B (14.1%), so production has enormous margin
 * and this bump is not needed there. It is kept because debug.conf is still a
 * supported configuration and 72 B is not a margin. */
#define SENSOR_UPDATE_STACK_SIZE  4096
/* 4096 rather than 2048, same reasoning as BUTTON_HANDLER_STACK_SIZE below:
 * this thread's own draw path (draw_clock_title() -> printLine() ->
 * drawText()/drawGlyph() -> SPI_Transmit()) is the same font/SPI depth as
 * button_handler_thread's menu draw, and change_ui_mode() calls ui_refresh()
 * directly (see ui.c), so this thread's stack absorbs that same call chain
 * too. It sits adjacent to button_handler_stack in memory — an overflow here
 * corrupting that neighboring stack (or vice versa) matches the usage-fault
 * signature (misaligned exception frame) caught live via GDB 2026-08-01,
 * which persisted even after fixing the ui_refresh()/handle_ui_input() race
 * with display_draw_mutex. */
#define UI_REFRESH_STACK_SIZE     4096
#define DISPLAY_TIMEOUT_STACK_SIZE 512
/* 4096 rather than 2048. 2048 was sized only for the FIFO-watermark path
 * (get_fifo_data() -> imu_process() -> nvs_log_record(), several frames deep
 * under NO_OPTIMIZATIONS) — it never accounted for handle_ui_input(), which
 * this thread also calls on every real button press and which walks into
 * change_ui_mode() -> updateMainMenuScreen() -> render_menu_items() -> font/
 * SPI drawing calls. Measured via GDB (2026-08-01) with the 2048B stack:
 * the CONFIG_INIT_STACKS 0xaa fill pattern was already overwritten down to
 * ~132 bytes from the bottom of the buffer — a hair from overflow — which
 * lines up with the board resetting specifically on the first real button
 * press once the UI is up (the one time this thread's display-draw path
 * actually runs). Same failure shape as the DISPLAY_TIMEOUT_STACK_SIZE 512B
 * overflow documented in CLAUDE.md. */
#define BUTTON_HANDLER_STACK_SIZE  4096  /* also drains the IMU FIFO, see below */

#define SENSOR_UPDATE_PRIORITY   7
#define UI_REFRESH_PRIORITY      7
#define DISPLAY_TIMEOUT_PRIORITY 7
#define BUTTON_HANDLER_PRIORITY  5  /* highest — buttons + IMU FIFO watermark */

K_THREAD_STACK_DEFINE(sensor_update_stack,   SENSOR_UPDATE_STACK_SIZE);
K_THREAD_STACK_DEFINE(ui_refresh_stack,      UI_REFRESH_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_timeout_stack, DISPLAY_TIMEOUT_STACK_SIZE);
K_THREAD_STACK_DEFINE(button_handler_stack,  BUTTON_HANDLER_STACK_SIZE);

static struct k_thread sensor_update_thread;
static struct k_thread ui_refresh_thread;
static struct k_thread display_timeout_thread;
static struct k_thread button_handler_thread;

/* Flash-maintenance pause handshake (DUMP/ERASE — src/comm/protocol.c via
 * app_pause_background_threads()). All three of sensor_update_thread,
 * ui_refresh_thread, and button_handler_thread touch the shared SPI1 bus
 * (NAND @0, IMU @1, display @2 all sit on one controller instance, so
 * Zephyr's spi_context lock is shared across them) — k_thread_suspend()ing
 * one of them mid-spi_transceive() would hold that lock forever and
 * deadlock the protocol thread's own NAND reads. Instead each thread is
 * given an extra wake source (bg_pause_wake_sem) and checks bg_pause_active
 * immediately on waking, before touching SPI: if set, it acks via
 * bg_quiesced_sem and blocks on bg_resume_sem — so a thread only ever parks
 * at its own wake boundary, never mid-transaction. */
static atomic_t bg_pause_active = ATOMIC_INIT(0);
K_SEM_DEFINE(bg_pause_wake_sem, 0, 3);
K_SEM_DEFINE(bg_quiesced_sem, 0, 3);
K_SEM_DEFINE(bg_resume_sem, 0, 3);

/* Called right after waking, before any SPI/NVS work. Returns true if this
 * iteration was consumed by parking (caller should skip its normal body and
 * loop back to waiting) rather than real work. */
static bool bg_park_if_paused(void)
{
    if (!atomic_get(&bg_pause_active)) {
        return false;
    }
    k_sem_give(&bg_quiesced_sem);
    k_sem_take(&bg_resume_sem, K_FOREVER);
    return true;
}

/* IMU temp/step-count update — every 9 s, triggered by timer1_sem. Wall-clock
 * time updates every UI refresh instead (1 s cadence, see ui_refresh_thread_entry)
 * since a 9 s-stale clock reads as broken to anyone glancing at the screen. */
static void sensor_update_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        struct k_poll_event events[2] = {
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &timer1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &bg_pause_wake_sem),
        };
        k_poll(events, 2, K_FOREVER);

        if (bg_park_if_paused()) {
            continue;
        }

        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&timer1_sem, K_NO_WAIT);
        } else {
            continue;
        }

        if (imu_alive) {
            ui_clock_set_temp(imu_get_temp());
            ui_clock_set_steps(imu_get_pedo());
        }

        /* Independent of imu_alive — the SoC's own die sensor works whether or
         * not the IMU came up, and is worth showing on a board where the IMU
         * is dead. Skipped silently if the driver never bound. */
        int16_t soc_centi = 0;   /* 0 if the read below fails; BLE reports it as-is */
        if (soc_temp_read_centi_c(&soc_centi) == 0) {
            ui_clock_set_soc_temp(soc_temp_centi_c_to_f(soc_centi));
        }

        int batt_mv = battery_voltage_mv();

        ui_clock_set_battery_mv(batt_mv);

        /* Feed the low-power policy from the same reading rather than taking a
         * second one: battery_voltage_mv() pulses the divider leg for its
         * sample, so each extra call costs a little energy of its own. */
        low_power_battery_update(batt_mv);

        /* Clock-face power indicators. battery_charging() is still stubbed
         * false (discrete BMS, no charge-status signal wired — see power.c),
         * so "CHG" reads not-charging until that lands; the badge is on
         * screen so the slot is reserved.
         *
         * "LP" now tracks the low-power MODE (user setting OR battery
         * trigger), not BOOST_SEL. It used to read power_save_is_enabled(),
         * which was always false — nothing ever called power_save_enable() —
         * and which in any case selects the more expensive rail. */
        ui_clock_set_charging(battery_charging() ? 1 : 0);
        ui_clock_set_low_power(low_power_is_active() ? 1 : 0);
        ui_clock_set_ble(ble_is_enabled() ? 1 : 0);

        /* Hand BLE the same snapshot the clock face just got. Deliberately
         * fed from here rather than sampled in the notify work: every value
         * below comes from SPI1 or I2C, and reading them from the Bluetooth
         * or system workqueue would put a thread on those buses that neither
         * the dump-pause handshake nor the measured stack budget knows about.
         * See ble_publish_status()'s comment. */
        ble_publish_status((uint16_t)batt_mv,
                           imu_alive ? getTempDataFromIMUReg() : 0,
                           soc_centi,
                           imu_alive ? imu_get_pedo() : 0,
                           (uint8_t)activity_current(),
                           activity_current_seq(),
                           imu_is_worn(),
                           rv3028_time_is_set());

        // battery_percent() (power.c) is implemented and ready to wire in — deliberately
        // not called yet. The clock face currently shows raw divider mV via
        // display_out_battery(), not percent, on purpose: validating the ADC/divider
        // reading itself is the current bring-up concern, separate from the LiPo
        // discharge-curve mapping (see the comment on display_out_battery() in
        // ui_display.c). Switch to percent once that validation is done.
        // ui_clock_set_battery(battery_percent(battery_voltage_mv()));
    }
}

/* UI refresh — every 1 s, triggered by timer2_sem. Updates the wall clock on
 * every tick (so the displayed time never looks stale) and drives the NVS
 * temp/anchor logging cadence (nvs_pipeline_tick() assumes one call/second). */
static void ui_refresh_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        struct k_poll_event events[2] = {
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &timer2_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &bg_pause_wake_sem),
        };
        k_poll(events, 2, K_FOREVER);

        if (bg_park_if_paused()) {
            continue;
        }

        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&timer2_sem, K_NO_WAIT);
        } else {
            continue;
        }

        ui_clock_set_time(get_current_time());
        ui_clock_set_date(get_date());

        if (display_status == 1) {
            /* Auto-off first, so a tick that crosses the timeout puts the
             * panel to sleep instead of drawing one more frame into it
             * (ui_refresh() itself no-ops while asleep). */
            ui_idle_tick();
            ui_refresh();
        }

        nvs_pipeline_tick();
    }
}

/* Display timeout — 9 s one-shot, triggered by timer3_sem. Body intentionally
 * inert for now (see CLAUDE.md "Known Issues" — display timeout is not yet
 * a defined feature); thread exists so the semaphore has a waiter.
 *
 * Disabled for bring-up 2026-07-28: creating this thread reproducibly
 * causes a display off/reboot/redraw cycle a few seconds in — thread body is
 * dead code (switch_display(0) commented out) and timer3 is never started,
 * so it's not the timeout logic itself; DISPLAY_TIMEOUT_STACK_SIZE (512 B) is
 * suspected too small for this NO_OPTIMIZATIONS build, overflowing into an
 * adjacent stack and eventually hard-faulting into a CPU lockup reset.
 * Confirmed via A/B: board stops resetting once this thread isn't created.
 * Re-enable (with a larger stack) once the timeout feature is implemented
 * for real — see project_thread_bringup_crash memory. */
static void display_timeout_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        k_sem_take(&timer3_sem, K_FOREVER);
        // switch_display(0);
    }
}

/* Hold-to-repeat tuning for the value-editing screens. The initial delay has
 * to be long enough that a normal single tap never repeats; the period is
 * what the value ramps at once repeating starts. */
#define BTN_REPEAT_DELAY_MS   450
#define BTN_REPEAT_PERIOD_MS  120

/* IMU INT1: drain the FIFO, then check whether the same edge was also a WOM
 * raise-to-wake. Factored out of button_handler_thread_entry()'s poll loop so
 * the auto-repeat loop below can keep servicing the IMU while a button is
 * held — see run_button_autorepeat(). */
static void service_imu_int1(void)
{
    if (!imu_alive) {
        return;
    }

    get_fifo_data();
    imu_process();

    /* Raise-to-wake v2: WOM is only the arming trigger now — the gesture is
     * confirmed in firmware from the accel samples the drain above just fed
     * into the posture ring (orientation + stillness + dwell), because WOM on
     * its own is a bare acceleration-magnitude test and woke the display for
     * anything vigorous, brushing teeth included. imu_check_raise_gesture()
     * consumes the WOM status itself; INT_STATUS2 has no other reader, so
     * this still can't race the FIFO drain. See imu.c for the full rationale
     * and the tuning constants. */
    if (imu_check_raise_gesture()) {
        ui_wake_display_if_asleep();
    }
}

/* Auto-repeat while a button is held down on a value-editing screen.
 *
 * Buttons are edge-driven (MCP23008 interrupt -> buttonN_sem), so without
 * this a held button is indistinguishable from a single tap and the user has
 * to click once per minute/hour of adjustment. Here we re-run
 * handle_ui_input() on a timer for as long as the button reads pressed.
 *
 * Deliberately narrow, in two ways:
 *  - Only on ui_autorepeat_active() modes (time/date setter, brightness).
 *  - Only for SW1/SW4 alone, the two value-change buttons. Repeating
 *    SELECT/BACK, or the setter's SW2 (NEXT SCREEN), would page through both
 *    screens and commit the time on a single long press. A multi-button mask
 *    (e.g. the SW3+SW4 home combo) also stops the loop rather than repeating.
 *
 * Blocking here does not stall IMU logging: the wait is a k_poll on
 * imu_int1_sem with an absolute deadline, so FIFO watermark interrupts are
 * serviced during the hold and the repeat still fires on schedule. It does
 * skip bg_park_if_paused() for the duration of the hold; that check runs
 * again the moment the button is released, and a hold is bounded by the
 * user's thumb. */
static void run_button_autorepeat(void)
{
    uint32_t interval = BTN_REPEAT_DELAY_MS;

    while (ui_autorepeat_active()) {
        uint8_t held = button_poll();

        if (held != BUTTON_1_MASK && held != BUTTON_4_MASK) {
            return;  /* released, or a mask we refuse to repeat */
        }

        int64_t deadline = k_uptime_get() + interval;
        int64_t remaining;

        while ((remaining = deadline - k_uptime_get()) > 0) {
            struct k_poll_event ev = K_POLL_EVENT_INITIALIZER(
                K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &imu_int1_sem);

            if (k_poll(&ev, 1, K_MSEC(remaining)) == 0 &&
                ev.state == K_POLL_STATE_SEM_AVAILABLE) {
                k_sem_take(&imu_int1_sem, K_NO_WAIT);
                service_imu_int1();
            }
        }

        /* handle_ui_input() re-reads the buttons itself and returns without
         * doing anything if the user let go during the wait, so a release
         * mid-interval can't sneak an extra step in. */
        handle_ui_input();
        interval = BTN_REPEAT_PERIOD_MS;
    }
}

/* Buttons + IMU FIFO watermark (INT1 — this board has no INT2, see
 * IMU_HAS_INT2 in interrupt.c). Buttons live on the MCP23008 expander, not
 * populated on this board yet — their semaphores simply never fire, which is
 * harmless; k_poll blocks on whichever events remain live. */

/* Long-press SW2 (top-right) = jump straight to the activity screen.
 *
 * Classifying the press costs time, and that is the whole subtlety here: this
 * cannot just return a bool and let the caller re-poll, because a SHORT press
 * is already released by the time we know it was short. Re-polling then reads
 * 0 and the press vanishes — which broke SELECT across the entire UI on the
 * first hardware test. Hence the three-way result: the caller dispatches a
 * short press explicitly via handle_ui_input_latched().
 *
 * The wait services IMU INT1 exactly like run_button_autorepeat() does, rather
 * than just sleeping — this thread is the only FIFO drain, and stalling it for
 * the whole hold window would let the FIFO run away. */
#define ACTIVITY_HOLD_MS   600
#define ACTIVITY_POLL_MS   20

typedef enum {
    BTN2_HOLD_CONSUMED,  /* long press: activity screen opened, nothing more to do */
    BTN2_SHORT,          /* released before the hold threshold — an ordinary SELECT */
    BTN2_PASSTHROUGH,    /* not classified; let handle_ui_input() poll as usual */
} btn2_result_t;

static btn2_result_t service_button2_hold(void)
{
    /* Display asleep: this press is a wake, not a shortcut. Hand it straight
     * to handle_ui_input() so its "first press only wakes" branch runs — and
     * so it polls, since the button is still down at this instant. */
    if (!display_is_awake()) {
        return BTN2_PASSTHROUGH;
    }

    int64_t deadline = k_uptime_get() + ACTIVITY_HOLD_MS;

    while (k_uptime_get() < deadline) {
        int64_t remaining = deadline - k_uptime_get();
        int64_t slice = remaining < ACTIVITY_POLL_MS ? remaining : ACTIVITY_POLL_MS;

        struct k_poll_event ev = K_POLL_EVENT_INITIALIZER(
            K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &imu_int1_sem);

        if (k_poll(&ev, 1, K_MSEC(slice)) == 0 &&
            ev.state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&imu_int1_sem, K_NO_WAIT);
            service_imu_int1();
        }

        if ((button_poll() & BUTTON_2_MASK) == 0) {
            /* Released inside the window. The press is OVER, so the caller
             * must dispatch it explicitly — polling again would read 0 and
             * drop it. This is exactly the bug that broke SELECT everywhere
             * on first hardware test. */
            return BTN2_SHORT;
        }
    }

    ui_open_activity_screen();

    /* Swallow the rest of the hold so the release does not immediately read
     * as a fresh SELECT on the screen we just opened (which would toggle a
     * session the user never asked for). */
    while (button_poll() & BUTTON_2_MASK) {
        k_msleep(ACTIVITY_POLL_MS);
    }
    return BTN2_HOLD_CONSUMED;
}

static void button_handler_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        struct k_poll_event events[6] = {
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button2_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button3_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button4_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &imu_int1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &bg_pause_wake_sem),
        };

        k_poll(events, 6, K_FOREVER);

        if (bg_park_if_paused()) {
            continue;
        }

        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button1_sem, K_NO_WAIT);
            handle_ui_input();
        }
        if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button2_sem, K_NO_WAIT);
            switch (service_button2_hold()) {
            case BTN2_HOLD_CONSUMED:
                break;
            case BTN2_SHORT:
                handle_ui_input_latched(BUTTON_2_MASK);
                break;
            case BTN2_PASSTHROUGH:
                handle_ui_input();
                break;
            }
        }
        if (events[2].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button3_sem, K_NO_WAIT);
            handle_ui_input();
        }
        if (events[3].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button4_sem, K_NO_WAIT);
            handle_ui_input();
        }
        if (events[4].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&imu_int1_sem, K_NO_WAIT);
            service_imu_int1();
        }

        /* No-op unless a value-editing screen is up and a button is still
         * physically down. */
        run_button_autorepeat();
    }
}

void app_pause_background_threads(void)
{
    /* Cooperative park, not k_thread_suspend(): all three background
     * threads share the SPI1 bus with the NAND driver (Zephyr's spi_context
     * lock is per-controller, shared across NAND@0/IMU@1/display@2), so
     * force-suspending one mid-spi_transceive() would hold that lock forever
     * and deadlock this thread's own NAND reads during a DUMP. Instead we
     * wake each thread and wait for it to park itself at its own wake
     * boundary (see bg_park_if_paused()), which only ever happens between
     * SPI transactions, never inside one. */
    atomic_set(&bg_pause_active, 1);
    k_sem_give(&bg_pause_wake_sem);
    k_sem_give(&bg_pause_wake_sem);
    k_sem_give(&bg_pause_wake_sem);

    k_sem_take(&bg_quiesced_sem, K_FOREVER);
    k_sem_take(&bg_quiesced_sem, K_FOREVER);
    k_sem_take(&bg_quiesced_sem, K_FOREVER);

    /* A thread may have woken (and acked) via its own normal trigger rather
     * than consuming a wake token, so up to 3 tokens can be left over —
     * clear them so they don't cause a spurious immediate re-wake next
     * cycle (see bg_pause_wake_sem comment above the struct definitions). */
    k_sem_reset(&bg_pause_wake_sem);
}

void app_resume_background_threads(void)
{
    atomic_set(&bg_pause_active, 0);
    k_sem_give(&bg_resume_sem);
    k_sem_give(&bg_resume_sem);
    k_sem_give(&bg_resume_sem);
}

static const char *usb_status_str(enum usb_dc_status_code status)
{
    switch (status) {
    case USB_DC_ERROR:        return "ERROR";
    case USB_DC_RESET:        return "RESET";
    case USB_DC_CONNECTED:    return "CONNECTED";
    case USB_DC_CONFIGURED:   return "CONFIGURED";
    case USB_DC_DISCONNECTED: return "DISCONNECTED";
    case USB_DC_SUSPEND:      return "SUSPEND";
    case USB_DC_RESUME:       return "RESUME";
    case USB_DC_INTERFACE:    return "INTERFACE";
    case USB_DC_SET_HALT:     return "SET_HALT";
    case USB_DC_CLEAR_HALT:   return "CLEAR_HALT";
    case USB_DC_SOF:          return "SOF";
    case USB_DC_UNKNOWN:      return "UNKNOWN";
    default:                  return "???";
    }
}

static void usb_status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
    /* SOF fires every 1 ms — far too noisy to log. */
    if (status == USB_DC_SOF) {
        return;
    }
    LOG_INF("USB status: %s (%d)", usb_status_str(status), status);
}

int main(void)
{
    int ret;

    LOG_INF("=== WWD-n bring-up: display + USB CDC ===");

    if (!device_is_ready(cdc_dev)) {
        LOG_ERR("cdc_acm_uart0 device not ready");
    } else {
        LOG_INF("cdc_acm_uart0 ready");
    }

    ret = usb_enable(usb_status_cb);
    if (ret != 0) {
        LOG_ERR("usb_enable() failed: %d", ret);
    } else {
        LOG_INF("usb_enable() ok");
    }

    /* Binary host<->device command channel — separate ttyACM from the
     * console/log stream above (cdc_acm_uart1, see nrf52833_ders.dts). */
    rate_config_init();
    protocol_init();

    /* Wait for a host terminal to attach (DTR) before the one-shot probe
     * output, otherwise it scrolls past before anyone is listening. A fixed
     * k_msleep() here used to race the host: connects landing just after it
     * lost the whole probe block. Bounded so an unattended boot still runs.
     *
     * NB this also delays the deferred device_init() below, so it hands the
     * RV-3028 a settle margin a shipped build would not have — see the
     * "settle:" measurement for the number that actually matters. */
    {
        uint32_t dtr = 0;
        int64_t t_dtr = k_uptime_get();

        while (!dtr && (k_uptime_get() - t_dtr) < 3000) {
            uart_line_ctrl_get(cdc_dev, UART_LINE_CTRL_DTR, &dtr);
            k_msleep(50);
        }
        /* DTR asserts the instant the host opens the port, but the reader on
         * the far side may not be consuming yet — 100 ms lost the first lines
         * of the block. Give it a full second. */
        k_msleep(1000);
    }

    /* Nail down VCC before probing anything on I2C. BOOST_SEL (MCP23008 GP6)
     * selects the TPS63900 rail, and the expander powers up with all pins as
     * inputs — so without this the mode select floats and the RV-3028 is
     * brought up on an undefined supply. power_init() below re-does this
     * harmlessly; it simply runs far too late to help the RTC. */
    power_rail_init();

    cdc_write("\r\n===== I2C / RV-3028-C7 probe =====\r\n");
    lfclk_report();
    if (!device_is_ready(i2c_dev)) {
        cdc_write("i2c0 NOT ready — bus driver failed to init\r\n");
    } else {
        i2c_bus_scan();
        rv3028_probe();

        /* rv3028_probe() above just did the deferred-init dance (rtc_wait_ready()
         * + device_init()) directly against the Zephyr device handle for
         * diagnostics — rv3028_init() (peripheral/rv3028.c) is a separate module
         * with its own static device handles, and must be called now that the
         * device is actually bound, or its own device_is_ready() check would see
         * an unbound device and permanently null out its handles. */
        rv3028_init();
    }
    cdc_write("==================================\r\n");

    imu_probe();

    display_phase();

    /* IMU first, NVS second — the 0f89f1e ordering. */
    nvs_bringup_phase();

    /* ---- Real system bring-up ---- */

    led_init();
    power_init();
    low_power_init();

    /* Non-fatal: a failure here only costs the SoC die-temperature reading
     * (clock-face badge + RECORD_SOC_TEMP), and soc_temp_read_centi_c() then
     * returns -ENODEV forever, which both call sites already skip on. */
    soc_temp_init();

    activity_init();

    /* Last of the subsystem inits: bt_enable() starts its own threads and
     * claims RTC0/TIMER0, so let everything that might contend for a bus or a
     * timer be up and settled first. Failure is non-fatal — ble_init() logs
     * and returns, and the device works exactly as before without it. */
    ble_init();

    init_buttons();       /* no-op-safe if MCP23008 absent, see button.c */
    init_button_buffer();
    config_all_interrupts();  /* button failures logged, non-fatal; see interrupt.c */

    init_ui();  /* checks display_status (set by display_phase() above) */

    LOG_INF("Creating application threads...");

    k_thread_create(&sensor_update_thread, sensor_update_stack,
                    K_THREAD_STACK_SIZEOF(sensor_update_stack),
                    sensor_update_thread_entry, NULL, NULL, NULL,
                    SENSOR_UPDATE_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&sensor_update_thread, "sensor_update");

    k_thread_create(&ui_refresh_thread, ui_refresh_stack,
                    K_THREAD_STACK_SIZEOF(ui_refresh_stack),
                    ui_refresh_thread_entry, NULL, NULL, NULL,
                    UI_REFRESH_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&ui_refresh_thread, "ui_refresh");

    /* display_timeout_thread stays disabled — see the comment above its
     * entry function (undersized-stack crash, feature not implemented yet). */
    // k_thread_create(&display_timeout_thread, display_timeout_stack,
    //                 K_THREAD_STACK_SIZEOF(display_timeout_stack),
    //                 display_timeout_thread_entry, NULL, NULL, NULL,
    //                 DISPLAY_TIMEOUT_PRIORITY, 0, K_NO_WAIT);
    // k_thread_name_set(&display_timeout_thread, "display_timeout");

    /* Re-enabled 2026-07-29: this is the only thread that drains the IMU
     * FIFO (get_fifo_data() + imu_process()) into NVS via the INT1 watermark
     * interrupt — without it, NVS_LOG_IMU_SAMPLES logging silently does
     * nothing despite being enabled. Button polling comes along for the
     * ride (harmless no-op — MCP23008 not populated on this board), but the
     * FIFO drain is the reason this needs to run. */
    k_thread_create(&button_handler_thread, button_handler_stack,
                    K_THREAD_STACK_SIZEOF(button_handler_stack),
                    button_handler_thread_entry, NULL, NULL, NULL,
                    BUTTON_HANDLER_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&button_handler_thread, "button_handler");

    LOG_INF("Starting WWD program!");
    init_timer();

    cdc_write("[boot] main() reached end, entering idle loop\r\n");

    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
