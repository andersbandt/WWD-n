//*****************************************************************************
//!
//! @file imu.c
//! @author Anders Bandt
//! @brief Contains functions for IMU control. Will try to keep code IC independent
//! @version 0.9
//! @date November 2023
//!
//*****************************************************************************


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
    // below 2 are for printf only (I think)
    #include <stdio.h>
    #include <stddef.h> 

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* My header files  */
#include <peripheral/interrupt.h>
#include <circular_buffer.h>
#include <memory/nvs.h>
#include <peripheral/clock.h>

/* IMU header files*/
#include <imu.h>

#ifdef USE_DERS_IMU
    #include <ICM_42670.h>
    #include <imu_process.h>
    #include <inv_imu_driver.h>
#else
    #include <icm42670.h>
#endif


LOG_MODULE_REGISTER(imu, LOG_LEVEL_INF);



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Circular_Buffer * imu_data_buffer = NULL;
uint32_t step_count;
int16_t imu_temperature = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*
 * imu_init: does register and data configuration for the IMU
 */
int imu_init() {
    LOG_INF("Initializing IMU ...");

    /* 64 slots: at 100 Hz with FIFO watermark of 50, we drain on every interrupt
     * and never accumulate more than ~50 events. 200 slots overflowed the 4KB heap
     * once accel_high_res/gyro_high_res were added to the event struct (6 bytes each). */
    imu_data_buffer = circular_buffer_init(64, sizeof(inv_imu_sensor_event_t));
    // LOG_INF("\nIMU data buffer setup");
    // LOG_INF("buffer = [%d]", imu_data_buffer->buffer);
    // LOG_INF("buffer_end = [%d]", imu_data_buffer->buffer_end);


    int rc = 0;
    #ifdef USE_DERS_IMU
        // do rough init
        rc = init_icm();
        if (rc != 0) {
            LOG_ERR("init_icm() failed: %d", rc);
            return rc;
        }

        // start IMU
        rc = imu_start();
        if (rc != 0) {
            LOG_ERR("imu_start() failed: %d", rc);
            return rc;
        }

        // actually configure and start IMU
        if (IMU_FIFO_ENABLED) {
            rc = imu_fifo_interrupt();
            if (rc != 0) {
                LOG_ERR("imu_fifo_interrupt() failed: %d", rc);
                return rc;
            }
        }
        if (IMU_APEX_ENABLED) {
            rc = imu_apex();
            if (rc != 0) {
                LOG_ERR("imu_apex() failed: %d", rc);
                return rc;
            }
        }

    #else
        rc |= icm42670_init();
        /* Set initial sample rates */
        rc |= icm42670_set_accel_rate(100); /* 100 Hz */
        rc |= icm42670_set_gyro_rate(100); /* 100 Hz */

        struct icm42670_data sensor_data;
        rc |= icm42670_read_all(&sensor_data);
        printf("Accel (m/s^2): X=%.2f, Y=%.2f, Z=%.2f\n",
            sensor_data.accel_x,
            sensor_data.accel_y,
            sensor_data.accel_z);

        printf("Gyro (dps): X=%.2f, Y=%.2f, Z=%.2f\n",
            sensor_data.gyro_x,
            sensor_data.gyro_y,
            sensor_data.gyro_z);
    #endif

    if (rc == 0) {
        LOG_INF("Initialized IMU\n");
    }
    else {
        LOG_ERR("Failed to initialize ICM42670 with code [%d]\n", rc);
    }

    return rc;
}
 

/*
 * imu_start: actually starts the accelerometer and gyroscope
 */
int imu_start() {
    int rc = 0;

    LOG_INF("Starting accel...");
    rc |= startAccel(100, 16);     // ODR=100 Hz, full-scale range=16

    LOG_INF("Starting gyro...");
    rc |= startGyro(100, 2000);    // ODR=100 Hz, full-scale range=2000 dps

    return rc;
}


/*
 * imu_apex: initializes APEX functionality
 */
int imu_apex() {
    int rc = 0;
    rc |= startApex();
    return rc;
}


/*
 * imu_fifo_interrupts: Enables the FIFO interrupt on the IMU
 */
int imu_fifo_interrupt() {
    LOG_INF("Enabling IMU interrupt for FIFO watermark level: %d", IMU_FIFO_WM);
    int rc = enableFifoInterrupt(IMU_FIFO_WM);
#if IMU_HIGH_RES_ENABLED
    rc |= inv_imu_enable_high_resolution_fifo(&icm_driver);
#endif
    return rc;
}



/*
 * imu_reg_poll: polls data and adds it to the circular buffer
 */
void imu_reg_poll() {
    inv_imu_sensor_event_t imu_event;

    int error = getDataFromIMUReg(&imu_event);
    if (error) {
        LOG_INF("\tgetDataFromIMUReg error: %d", error);
    }

//    event_print(&imu_event);

    /* ADD TO CIRCULAR BUFFER */
//    bool added = 0;
    // circular_buffer_add(imu_data_buffer, &imu_event);

//    if (!added) {
//        LOG_INF(display, 0, 0, "ERROR in adding to circular buffer. Probably full");
//    }
}


/*
 * get_fifo_data: reads data from the FIFO
 */
// TODO: really should document the flow. Where the event callback is stored, all the functions involved, circular buffer, etc
void get_fifo_data() {
    int fifo_status = getDataFromFifo();
}


/*
 * imu_deque: returns the last IMU event on the buffer
 */
inv_imu_sensor_event_t imu_deque() {
    inv_imu_sensor_event_t event;
    circular_buffer_remove(imu_data_buffer, &event);
    return event;
}


/*
 * imu_get_temp: function to return temperature from IMU
 */
int16_t imu_get_temp() {
    #ifdef USE_DERS_IMU
        int16_t imu_temp = getTempDataFromIMUReg();
    #else
        int16_t imu_temp = 100;
    #endif
    
    /* float temp_celsius = ((float)imu_temp / 128.0f) + 25.0f; */
    /* int16_t imu_c = (imu_temp / 128) + 25; */
    int16_t imu_f = ((imu_temp / 128) + 25) * 1.8 + 32;

    return imu_f;
}


/*
 * imu_get_pedo
 */
int imu_get_pedo() {
    float step_cadence = 0;
    const char* activity[20]; // NOTE: I think this thing will be something like "walking, running, etc?"

    uint32_t count = 0;

    #ifdef USE_DERS_IMU
        volatile int status = getPedometer(&count, &step_cadence, activity);
    #else
        volatile int status = 999;
    #endif
        step_count = count;

    if (status == 0) {
        step_count = count;
    }

    return step_count;
}


/*
 * imu_process: this function currently processes the circular buffers of raw data
 */
void imu_process() {
    inv_imu_sensor_event_t event;

    while (!circular_buffer_empty(imu_data_buffer)) {
        circular_buffer_remove(imu_data_buffer, &event);
        event_print(&event);

        struct record_imu_fifo sample = {
            .accel = { event.accel[0], event.accel[1], event.accel[2] },
#if ICM_IS_GYRO_SUPPORTED
            .gyro  = { event.gyro[0],  event.gyro[1],  event.gyro[2]  },
#else
            .gyro  = { 0, 0, 0 },
#endif
        };
        nvs_log_record(RECORD_IMU_FIFO, &sample, sizeof(sample), get_dt_ticks());
    }
}


/*
 * imu_log: logs IMU data to NVS
 */
int imu_log(void) {
    int ret = 0;

    struct record_temperature temp = { .raw = imu_temperature };
    ret |= nvs_log_record(RECORD_TEMPERATURE, &temp, sizeof(temp), get_dt_ticks());

    struct record_step_count steps = { .steps = step_count };
    ret |= nvs_log_record(RECORD_STEP_COUNT, &steps, sizeof(steps), 0);

    return ret;
}




