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
#include <display.h>

/* NVS bring-up phase + pipeline tick */
#include <memory/nvs_bringup.h>

#include "peripheral/clock.h"   /* get_current_time() */
#include "peripheral/rv3028.h"
#include "peripheral/rv3028_bringup.h"
#include "peripheral/interrupt.h"
#include "peripheral/timer.h"
#include "hardware/led.h"
#include "hardware/button.h"
#include "power/power.h"
#include <ui.h>

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
#define SENSOR_UPDATE_STACK_SIZE  2048
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

        ui_clock_set_battery_mv(battery_voltage_mv());

        /* Clock-face power indicators. battery_charging() is still stubbed
         * false (discrete BMS, no charge-status signal wired — see power.c),
         * so "CHG" reads not-charging until that lands; the badge is on
         * screen so the slot is reserved. "LP" tracks BOOST_SEL. */
        ui_clock_set_charging(battery_charging() ? 1 : 0);
        ui_clock_set_low_power(power_save_is_enabled() ? 1 : 0);

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

/* Buttons + IMU FIFO watermark (INT1 — this board has no INT2, see
 * IMU_HAS_INT2 in interrupt.c). Buttons live on the MCP23008 expander, not
 * populated on this board yet — their semaphores simply never fire, which is
 * harmless; k_poll blocks on whichever events remain live. */
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
            handle_ui_input();
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
            if (imu_alive) {
                get_fifo_data();
                imu_process();

                /* Raise-to-wake v1 (WOM only, no tilt confirm — see
                 * imu_notes.md 2026-08-22). WOM shares INT1 with FIFO_THS;
                 * imu_check_wom() only ever reads INT_STATUS2, which nothing
                 * else touches, so this can't race the FIFO drain above. */
                if (imu_check_wom()) {
                    ui_wake_display_if_asleep();
                }
            }
        }
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
