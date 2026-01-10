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

/* My driver files */
#include <hardware/led.h>
#include <hardware/button.h>
#include <peripheral/interrupt.h>
#include <peripheral/timer.h>
#include <display.h>
#include <ui.h>
#include <imu.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


int cntr = 0;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern int display_status;

// BELOW ARE ACTUALLY NOT GLOBAL but probably should be
bool imu_status;


/* Thread stack sizes */
#define CLOCK_UPDATE_STACK_SIZE 2048
#define UI_REFRESH_STACK_SIZE 2048
#define DISPLAY_TIMEOUT_STACK_SIZE 1024
#define BUTTON_HANDLER_STACK_SIZE 2048

/* Thread priorities (lower number = higher priority) */
#define CLOCK_UPDATE_PRIORITY 7
#define UI_REFRESH_PRIORITY 7
#define DISPLAY_TIMEOUT_PRIORITY 7
#define BUTTON_HANDLER_PRIORITY 5  /* Higher priority for user input */

/* Thread stacks */
K_THREAD_STACK_DEFINE(clock_update_stack, CLOCK_UPDATE_STACK_SIZE);
K_THREAD_STACK_DEFINE(ui_refresh_stack, UI_REFRESH_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_timeout_stack, DISPLAY_TIMEOUT_STACK_SIZE);
K_THREAD_STACK_DEFINE(button_handler_stack, BUTTON_HANDLER_STACK_SIZE);

/* Thread control blocks */
struct k_thread clock_update_thread;
struct k_thread ui_refresh_thread;
struct k_thread display_timeout_thread;
struct k_thread button_handler_thread;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! THREAD ENTRY FUNCTIONS ------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// /* Increment counter and check for UI mode change */
// cntr++;
// if (cntr == 2) {
//     change_ui_mode(1);
// }


/**
 * @brief Clock/IMU/BMS update thread
 *
 * Triggered every 9 seconds by timer1
 */
void clock_update_thread_entry(void *p1, void *p2, void *p3) {
    while (1) {
        /* Wait for timer1 semaphore */
        k_sem_take(&timer1_sem, K_FOREVER);

        /* Update IMU pedometer */
        // TODO: Add IMU update code

        /* Update BMS */
        // TODO: Add BMS update code
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
 * @brief Button handler thread
 *
 * Handles button press events
 */
void button_handler_thread_entry(void *p1, void *p2, void *p3) {
    while (1) {
        /* Wait for any button press (using k_poll would be more efficient for multiple semaphores) */
        struct k_poll_event events[3] = {
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &button1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &button2_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &button3_sem),
        };

        k_poll(events, 3, K_FOREVER);

        /* Check which button was pressed */
        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button1_sem, K_NO_WAIT);

            /* Button 1 pressed */
            // cntr = 0;
            // change_ui_mode(2);
            led_blink(1);
            handle_ui_input();
        }

        if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button2_sem, K_NO_WAIT);

            /* Button 2 pressed */
            cntr = 0;

            /* Toggle display */
            // display_state = !display_state;
            // switch_display(display_state);
            // timer_start(3);

            /* Handle UI input */
            handle_ui_input();
        }

        if (events[2].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button3_sem, K_NO_WAIT);
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
    // run initialization functions
    led_init();
	led_fast_blink(1, 10);

    init_buttons();

    /*
    DISPLAY and UI config
    */
    led_set(1, 1);
    // init_display();
    // TODO: bundle this `display_status` into the init_ui statement
    if (display_status == 1) {
        init_ui();
    }
    led_set(1, 0);


    // initialize interrupts
    config_all_interrupts();


    LOG_INF("Starting WWD program!");

    /*
    NVS CONFIG BLOCK
    */
	// nvs_init();
    // static uint8_t data[2176];
    // data[0] = 8;
    // data[1] = 9;
    // nvs_write(&data, 2176);
    // ret = nvs_read(data, 2176, 0);
    // for (int i = 0; i < 10; i++) {
    //     printk("%02X ", data[i]);
    // }
    // printk("\n");
    /*
    END OF NVS CONFIG BLOCK
    */


    /*
    IMU CONFIG BLOCK
    */
    // struct icm42670_data sensor_data;
    // int ret = 0;
    // ret |= icm42670_init();
    // if (ret == 0) {
    //     printk("Initialized IMU\n");
    //     imu_status = true;
    //     ret = icm42670_set_accel_rate(100); /* 100 Hz */
    //     ret |= icm42670_set_gyro_rate(100); /* 100 Hz */
    // }
    // else {
    //     printk("Failed to initialize ICM42670 with code [%d]\n", ret);
    //     imu_status = false;
    //     led_set(1, 1);
    // }
    /*
    END OF IMU CONFIG BLOCK
    */

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

    /* Create UI refresh thread */
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

    LOG_INF("All threads created successfully");

    /* Main thread can now sleep - all work is done by worker threads */
    init_timer();
    while (1) {
        k_sleep(K_FOREVER);
    }

	return 0;
}

