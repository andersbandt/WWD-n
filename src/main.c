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
// TODO: give more thought to these stack sizes (pairs with next TODO down below about stack printing)
#define CLOCK_UPDATE_STACK_SIZE 1024
#define UI_REFRESH_STACK_SIZE 1024
#define DISPLAY_TIMEOUT_STACK_SIZE 512
#define BUTTON_HANDLER_STACK_SIZE 1024

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

        /* Update IMU */
        // imu_reg_poll();

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

        // size_t free_stack = 2000;
        // k_thread_stack_space_get(&ui_refresh_thread, &free_stack);
        // LOG_INF("ui_refresh  free: %d", free_stack);
        // k_thread_stack_space_get(&button_handler_thread, &free_stack);
        // LOG_INF("btn_handler free: %d", free_stack);

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
        /* Wait for any button press or IMU interrupt */
        struct k_poll_event events[6] = {
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
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &imu_int1_sem),
            K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                     K_POLL_MODE_NOTIFY_ONLY,
                                     &imu_int2_sem),
        };

        k_poll(events, 6, K_FOREVER);

        /* Button 1 pressed */
        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button1_sem, K_NO_WAIT);
            change_ui_mode(UI_MODE_MENU);
            handle_ui_input();
        }

        /* Button 2 pressed */
        if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button2_sem, K_NO_WAIT);


            /* Toggle display */
            // display_state = !display_state;
            // switch_display(display_state);
            // timer_start(3);
            handle_ui_input();
        }

        /* Button 3 */
        if (events[2].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button3_sem, K_NO_WAIT);
            handle_ui_input();
        }

        /* Button 4 */
        if (events[3].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&button4_sem, K_NO_WAIT);
            handle_ui_input();
        }

        /* IMU INT1 */
        if (events[4].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&imu_int1_sem, K_NO_WAIT);
            /* TODO: Add IMU INT1 handling code */
            LOG_INF("IMU INT1 triggered");
            led_set(2, 1);
            get_fifo_data();
        }

        /* IMU INT2 */
        if (events[5].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&imu_int2_sem, K_NO_WAIT);
            /* TODO: Add IMU INT2 handling code */
            LOG_INF("IMU INT2 triggered");
            led_set(3, 1);
            get_fifo_data();
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
    config_all_interrupts();

    /*
    DISPLAY and UI config
    */
    // led_set(1, 1);
    // init_display();
    // led_set(1, 0);

    // TODO: bundle this `display_status` into the init_ui statement
    if (display_status == 1) {
        init_ui();
    }

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
    int ret = 0;
    ret |= imu_init();
    if (ret == 0) {
        imu_status = true;
        k_msleep(2000);
    }
    else {
        imu_status = false;
        led_set(3, 1);
    }
    // get_fifo_data();
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
    // k_thread_create(&display_timeout_thread, display_timeout_stack,
    //                 K_THREAD_STACK_SIZEOF(display_timeout_stack),
    //                 display_timeout_thread_entry,
    //                 NULL, NULL, NULL,
    //                 DISPLAY_TIMEOUT_PRIORITY, 0, K_NO_WAIT);
    // k_thread_name_set(&display_timeout_thread, "display_timeout");

    /* Create button handler thread */
    k_thread_create(&button_handler_thread, button_handler_stack,
                    K_THREAD_STACK_SIZEOF(button_handler_stack),
                    button_handler_thread_entry,
                    NULL, NULL, NULL,
                    BUTTON_HANDLER_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&button_handler_thread, "button_handler");


    /* Main thread can now sleep - all work is done by worker threads */
    LOG_INF("Starting WWD program!");
    init_timer();


    while (1) {
        k_sleep(K_FOREVER);
    }

	return 0;
}

