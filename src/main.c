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
 * i2c_transfer) or IMU SPI reads, several calls deep, under this project's
 * CONFIG_NO_OPTIMIZATIONS build. 1024B undersized display_timeout_thread's
 * far simpler body enough to overflow into a CPU-lockup reset (see
 * project_thread_bringup_crash memory) — these threads do considerably more
 * per tick and had never actually been runtime-exercised with this real
 * logic before, so don't assume 1024B was ever validated for it. */
#define SENSOR_UPDATE_STACK_SIZE  2048
#define UI_REFRESH_STACK_SIZE     2048
#define DISPLAY_TIMEOUT_STACK_SIZE 512
#define BUTTON_HANDLER_STACK_SIZE  1024  /* also drains the IMU FIFO, see below */

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

/* IMU temp/step-count update — every 9 s, triggered by timer1_sem. Wall-clock
 * time updates every UI refresh instead (1 s cadence, see ui_refresh_thread_entry)
 * since a 9 s-stale clock reads as broken to anyone glancing at the screen. */
static void sensor_update_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        k_sem_take(&timer1_sem, K_FOREVER);

        if (imu_alive) {
            ui_clock_set_temp(imu_get_temp());
            ui_clock_set_steps(imu_get_pedo());
        }

        // TODO: enable when BMS is ready
        // ui_clock_set_battery(battery_percent());
        // ui_clock_set_charging(battery_charging());
    }
}

/* UI refresh — every 1 s, triggered by timer2_sem. Updates the wall clock on
 * every tick (so the displayed time never looks stale) and drives the NVS
 * temp/anchor logging cadence (nvs_pipeline_tick() assumes one call/second). */
static void ui_refresh_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        k_sem_take(&timer2_sem, K_FOREVER);

        ui_clock_set_time(get_current_time());

        if (display_status == 1) {
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
        struct k_poll_event events[5] = {
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button2_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button3_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &button4_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &imu_int1_sem),
        };

        k_poll(events, 5, K_FOREVER);

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
            }
        }
    }
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

    /* display_timeout_thread and button_handler_thread stay disabled — see
     * the comments above their entry functions. Neither is required for the
     * clock/temp/step-count display path: temp and step count are direct
     * IMU register reads (not FIFO-dependent), and buttons live on the
     * MCP23008 expander, not populated on this board. */
    // k_thread_create(&display_timeout_thread, display_timeout_stack,
    //                 K_THREAD_STACK_SIZEOF(display_timeout_stack),
    //                 display_timeout_thread_entry, NULL, NULL, NULL,
    //                 DISPLAY_TIMEOUT_PRIORITY, 0, K_NO_WAIT);
    // k_thread_name_set(&display_timeout_thread, "display_timeout");

    // k_thread_create(&button_handler_thread, button_handler_stack,
    //                 K_THREAD_STACK_SIZEOF(button_handler_stack),
    //                 button_handler_thread_entry, NULL, NULL, NULL,
    //                 BUTTON_HANDLER_PRIORITY, 0, K_NO_WAIT);
    // k_thread_name_set(&button_handler_thread, "button_handler");

    LOG_INF("Starting WWD program!");
    init_timer();

    cdc_write("[boot] main() reached end, entering idle loop\r\n");

    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
