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
#include <nvs.h>
// #include <imu.h>
#include <icm42670.h>
#include <display/display.h>



LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


bool imu_status;


int main(void)
{
    // run initialization functions
    led_init();
	led_fast_blink(10);

    // initialize interrupts
    config_all_interrupts();
    LOG_INF("Starting WWD program!");

    led_set(1, 1);
    init_display();
    led_set(1, 0);

    /*
    IMU CONFIG BLOCK
    */
    // struct icm42670_data sensor_data;
    // int ret = 0;
    // ret |= icm42670_init();
    //     // ret |= imu_init();
    // if (ret == 0) {
    //     printk("Initialized IMU\n");
    //     imu_status = true;
    //     /* Set initial sample rates */
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
            led_blink(2);
            BUTTON_1_INT_FLAG = 0;
        }
        if (BUTTON_2_INT_FLAG) {
            led_blink(3);
            BUTTON_2_INT_FLAG = 0;
        }

        k_msleep(30);
        k_msleep(3000);
    }
	return 0;
}

