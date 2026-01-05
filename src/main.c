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
#include <peripheral/interrupt.h>
#include <peripheral/timer.h>
#include <display.h>
#include <ui.h>
#include <imu.h>



LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


bool imu_status;
int cntr = 0;
bool display_state = 1;


int main(void)
{
    // run initialization functions
    led_init();
	led_fast_blink(10);

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
    DISPLAY and UI config
    */
    led_set(1, 1);
    init_display();
    led_set(1, 0);
    init_ui();

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


    // main loop
    while (1) {
        // LOG_INF("heartbeat ...");
        led_fast_blink(30);

        // process IMU
        // if (imu_status) {
        //     ret = icm42670_read_all(&sensor_data);
        //     if (ret == 0) {
        //         printf("Accel (m/s^2): X=%.2f, Y=%.2f, Z=%.2f\n",
        //             sensor_data.accel_x,
        //             sensor_data.accel_y,
        //             sensor_data.accel_z);

        //         printf("Gyro (dps): X=%.2f, Y=%.2f, Z=%.2f\n",
        //             sensor_data.gyro_x,
        //             sensor_data.gyro_y,
        //             sensor_data.gyro_z);
        //     }
        // }

        // handle interrupts
        if (BUTTON_1_INT_FLAG) {
            cntr = 0;
            change_ui_mode(2);
            handle_ui_input();
            BUTTON_1_INT_FLAG = 0;
        }
        if (BUTTON_2_INT_FLAG) {
            cntr = 0;

            // toggle display
            display_state = !display_state;
            switch_display(display_state);
            timer_start(3);

            // handle UI input
            handle_ui_input();
            BUTTON_2_INT_FLAG = 0;
        }


        // TIMER 1 (main "meat" of what would be my RTOS tasks)
        if (TIMER_1_DONE) {
            // update IMU ped
            
            // update BMS

            // increment counter for something?
            cntr++;
            if (cntr == 2) {
                change_ui_mode(1);
            }
            TIMER_1_DONE = 0;
        }

        // TIMER 2 (ui_refresh)
        if (TIMER_2_DONE) {
            if (display_state == 1) {
                ui_refresh();
            }
            TIMER_2_DONE = 0;
        }
        // TIMER 3 (auto-off display)
        if (TIMER_3_DONE) {
            switch_display(0);
            display_state = 0;
            TIMER_3_DONE = 0;
        }

        k_msleep(30);
        k_msleep(3000);
    }
	return 0;
}

