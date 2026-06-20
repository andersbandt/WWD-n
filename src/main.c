//*****************************************************************************
//!
//! @file led.h
//! @author Anders Bandt
//! @brief Main code for WWD with Nordic
//! @version 0.9
//! @date December 2025
//!
//*****************************************************************************

/* standard C file */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

/* My driver files */
#include <hardware/led.h>
#include <hardware/button.h>
#include <peripheral/interrupt.h>
// #include <ble/ble.h>
#include <peripheral/timer.h>
#include <peripheral/clock.h>
#include <peripheral/rtc.h>
#include <display.h>
#include <ui.h>
#include <imu.h>
#include <nvs.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


int cntr = 0;
bool imu_status;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern int display_status;


/* Thread stack sizes */
// TODO: give more thought to these stack sizes
        // size_t free_stack = 2000;
        // k_thread_stack_space_get(&ui_refresh_thread, &free_stack);
        // LOG_INF("ui_refresh  free: %d", free_stack);
        // k_thread_stack_space_get(&button_handler_thread, &free_stack);
        // LOG_INF("btn_handler free: %d", free_stack);

#define CLOCK_UPDATE_STACK_SIZE    1024
#define UI_REFRESH_STACK_SIZE      1024
#define DISPLAY_TIMEOUT_STACK_SIZE 512
#define BUTTON_HANDLER_STACK_SIZE  512
#define IMU_STACK_SIZE             4096

/* Thread priorities (lower number = higher priority) */
#define CLOCK_UPDATE_PRIORITY    7
#define UI_REFRESH_PRIORITY      7
#define DISPLAY_TIMEOUT_PRIORITY 7
#define BUTTON_HANDLER_PRIORITY  5  /* Highest user priority — buttons only */
#define IMU_PRIORITY             6  /* FIFO drain, processing, NVS logging */

/* Thread stacks */
K_THREAD_STACK_DEFINE(clock_update_stack,    CLOCK_UPDATE_STACK_SIZE);
K_THREAD_STACK_DEFINE(ui_refresh_stack,      UI_REFRESH_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_timeout_stack, DISPLAY_TIMEOUT_STACK_SIZE);
K_THREAD_STACK_DEFINE(button_handler_stack,  BUTTON_HANDLER_STACK_SIZE);
K_THREAD_STACK_DEFINE(imu_stack,             IMU_STACK_SIZE);

/* Thread control blocks */
struct k_thread clock_update_thread;
struct k_thread ui_refresh_thread;
struct k_thread display_timeout_thread;
struct k_thread button_handler_thread;
struct k_thread imu_thread;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! THREAD ENTRY FUNCTIONS ------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: what is this doing here?
// void dump_task(void)
// {
//     if (dump.active) {
//         dump_send_chunk(&dump);
//     }
// }


/**
 * @brief Clock/IMU/BMS update thread
 *
 * Triggered by timer1
 */
void clock_update_thread_entry(void *p1, void *p2, void *p3) {
    while (1) {
        /* Wait for timer1 semaphore */
        k_sem_take(&timer1_sem, K_FOREVER);

        // Update clock data (automatically marks dirty)
        ui_clock_set_time(get_current_time());
        // ui_clock_set_temp(imu_get_temp());

        // TODO: Enable when BMS is ready
        // ui_clock_set_battery(read_battery_percent());
        // ui_clock_set_charging(read_charging_status());

        // ui_clock_set_steps(step_count);
    }
}

/**
 * @brief UI refresh thread
 *
 * Triggered every 1 second by timer2
 */
void ui_refresh_thread_entry(void *p1, void *p2, void *p3) {
    while (1) {
        /* Wait for timer2 semaphore */
        k_sem_take(&timer2_sem, K_FOREVER);

        /* Refresh UI if display is on */
        if (display_status == 1) {
            ui_refresh();
        }
    }
}

/**
 * @brief Display timeout thread
 *
 * Triggered after 9 seconds by timer3 (one-shot)
 */
void display_timeout_thread_entry(void *p1, void *p2, void *p3) {
    while (1) {
        /* Wait for timer3 semaphore */
        k_sem_take(&timer3_sem, K_FOREVER);

        /* Turn off display */
        // switch_display(0);
        // display_state = 0;
        // change_ui_mode(1);
    }
}


/**
 * @brief IMU thread
 *
 * Handles all IMU interrupt events at priority 6, keeping SPI-heavy work
 * (FIFO drain, NVS logging) out of the high-priority button handler.
 *
 * INT1 — WOM / APEX events
 * INT2 — FIFO watermark: drain FIFO → circular buffer → process → log NVS
 */
void imu_thread_entry(void *p1, void *p2, void *p3) {
    while (1) {
        struct k_poll_event events[2] = {
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &imu_int1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &imu_int2_sem),
        };

        k_poll(events, 2, K_FOREVER);

        /* INT1 — WOM / APEX */
        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&imu_int1_sem, K_NO_WAIT);
            LOG_DBG("IMU INT1 triggered");
            led_fast_blink(2, 10);
        }

        /* INT2 — FIFO watermark */
        if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&imu_int2_sem, K_NO_WAIT);
            LOG_DBG("IMU INT2 triggered");
            if (imu_status) {
                get_fifo_data();
                imu_process();
            }
        }
    }
}


/**
 * @brief Button handler thread
 *
 * Handles button press events only — no IMU work here.
 */
void button_handler_thread_entry(void *p1, void *p2, void *p3) {
    while (1) {
        struct k_poll_event events[4] = {
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &button1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &button2_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &button3_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &button4_sem),
        };

        k_poll(events, 4, K_FOREVER);

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
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! MAIN FUNCTION ---------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(void)
{
    /* Wait for USB CDC ACM host to connect before logging anything.
     * Comment out before shipping — blocks boot until a terminal opens. */
    const struct device *usb_uart = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
    uint32_t dtr = 0;
    while (!dtr) {
        uart_line_ctrl_get(usb_uart, UART_LINE_CTRL_DTR, &dtr);
        k_sleep(K_MSEC(100));
    }

    // run initialization functions
    led_init();
	led_fast_blink(1, 10);

    // init clocking
    rtc_init();
    // ble_init();

    // init GPIO
    init_buttons();
    init_button_buffer();
    config_all_interrupts();


    /*
    DISPLAY and UI config
    */
    // led_set(1, 1);
    // init_display();
    // led_set(1, 0);
    // init_ui();
    /*
    END OF UI CONFIG
    */


    /*
    IMU CONFIG BLOCK
    */
    k_msleep(200);
    int ret = 0;
    ret |= imu_init();
    if (ret == 0) {
        imu_status = true;
    }
    else {
        imu_status = false;
        led_set(3, 1); 
    }
    /*
    END OF IMU CONFIG BLOCK
    */


    /*
    NVS CONFIG BLOCK
    */
    // NOTE: the init order of these might matter ... couldn't get IMU to init properly when it was after
    k_msleep(200);
	nvs_init();
    led_set(2, 1);
    nvs_dump();
    /*
    END OF NVS CONFIG BLOCK
    */


    led_set(2, 0);

    /*
    CREATE THREADS
    */
    LOG_INF("Creating application threads...");

    /* Create clock/IMU/BMS update thread */
    k_thread_create(&clock_update_thread, clock_update_stack,
                    K_THREAD_STACK_SIZEOF(clock_update_stack),
                    clock_update_thread_entry,
                    NULL, NULL, NULL,
                    CLOCK_UPDATE_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&clock_update_thread, "clock_update");

    // /* Create UI refresh thread */
    k_thread_create(&ui_refresh_thread, ui_refresh_stack,
                    K_THREAD_STACK_SIZEOF(ui_refresh_stack),
                    ui_refresh_thread_entry,
                    NULL, NULL, NULL,
                    UI_REFRESH_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&ui_refresh_thread, "ui_refresh");

    /* Create display timeout thread */
    k_thread_create(&display_timeout_thread, display_timeout_stack,
                    K_THREAD_STACK_SIZEOF(display_timeout_stack),
                    display_timeout_thread_entry,
                    NULL, NULL, NULL,
                    DISPLAY_TIMEOUT_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&display_timeout_thread, "display_timeout");

    /* Create button handler thread */
    k_thread_create(&button_handler_thread, button_handler_stack,
                    K_THREAD_STACK_SIZEOF(button_handler_stack),
                    button_handler_thread_entry,
                    NULL, NULL, NULL,
                    BUTTON_HANDLER_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&button_handler_thread, "button_handler");

    /* Create IMU thread */
    k_thread_create(&imu_thread, imu_stack,
                    K_THREAD_STACK_SIZEOF(imu_stack),
                    imu_thread_entry,
                    NULL, NULL, NULL,
                    IMU_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&imu_thread, "imu");


    /* Main thread can now sleep - all work is done by worker threads */
    LOG_INF("Starting WWD program!");
    init_timer();

    while (1) {
        k_sleep(K_FOREVER);
    }

	return 0;
}

