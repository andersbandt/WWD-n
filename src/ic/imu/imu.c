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
#include "inv_imu_driver.h"
#include <stdint.h>

/* Driver Header files  */
#include <config/ti_drivers_config.h>
#include <ti/display/Display.h>

/* My header files  */
#include <src/interrupts.h>
#include <src/circular_buffer.h>
#include <src/memory/nvs.h>
#include <src/clock.h>

#include <src/ic/imu/imu.h>
#include <src/ic/imu/ICM_42670.h>
#include <src/ic/imu/imu_process.h>
#include <src/ic/imu/inv_imu_driver.h>


uint8_t irq_received = 0;

extern Circular_Buffer *imu_data_buffer;


volatile uint32_t step_count;


#define FLASH_INTEGRITY_WRITE_CYCLE              10
int flash_write_num = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*
 * imu_init: does register and data configuration for the IMU
 */
int imu_init(Display_Handle display) {
    int rc = 0;
    rc |= init_icm(display);

    imu_data_buffer = circular_buffer_init(200, sizeof(inv_imu_sensor_event_t));
    LOG_INF(display, 0, 0, "\nIMU data buffer setup");
    LOG_INF(display, 0, 0, "buffer = [%d]", imu_data_buffer->buffer);
    LOG_INF(display, 0, 0, "buffer_end = [%d]", imu_data_buffer->buffer_end);

    if (!rc) {
        return 1;
    }
    else {
        return 0;
    }
}
 

/*
 * imu_start: actually starts the accelerometer and gyroscope
 */
int imu_start(Display_Handle display) {
    int rc = 0;

    LOG_INF(display, 0, 0, "\nStarting accel...");
    rc |= startAccel(100, 16);     // ODR=100 Hz, full-scale range=16

    LOG_INF(display, 0, 0, "Starting gyro...");
    rc |= startGyro(100, 2000);    // ODR=100 Hz, full-scale range=2000 dps

    if (!rc) {
        return 1;
    }
    else {
        return 0;
    }
}


/*
 * imu_apex: initializes APEX functionality
 */
int imu_apex(Display_Handle display) {
    int rc = 0;
    rc |= startApex(display);
    return rc;
}


/*
 * imu_deque: returns the last IMU event on the buffer
 */
inv_imu_sensor_event_t  imu_deque() {
    inv_imu_sensor_event_t event;
    circular_buffer_remove(imu_data_buffer, &event);
    return event;
}


/*
 * imu_process: this function currently processes the circular buffers of raw data
 */
/* // handle sample cycle spacing integrity check */
/* } */
void imu_process(Display_Handle display) {
    inv_imu_sensor_event_t event;

    if (!circular_buffer_empty(imu_data_buffer)) {
        // get current buffer count
        /* size_t buf_count = circular_buffer_get_count(imu_data_buffer); */
        /* LOG_INF(display, 0, 0, "buf count: [%d]\n", buf_count); */

        while (!circular_buffer_empty(imu_data_buffer)) {
            circular_buffer_remove(imu_data_buffer, &event);
            event_print(display, &event);
        }
    }
} // end of function



void write_to_flash() {
/* if (flash_write_num % FLASH_INTEGRITY_WRITE_CYCLE == 0) { */
/*     uint8_t flash_cycle_pad = 0xD8; */
/*     status = nvs_write_auto_offset(&flash_cycle_pad, 1); */
/*     LOG_INF(display, 0, 0, "\tFlash cycle integrity write. Write status: [%d]", status); */
//////////////////////////////////////////////
// WRITE TO FLASH MEMORY /////////////////////
//////////////////////////////////////////////
/*     status = nvs_write_auto_offset(&a_x, 2); */
/*     if (status != 0) { */
/*         LOG_INF(display, 0, 0, "\tERROR: NVS write for the IMU got status: [%d]", status); */
/*         nvs_error(status, 0, display); */
/*     } */
        /*     flash_write_num++; */
}


/*
 * imu_fifo_interrupts: Enables the FIFO interrupt on the IMU
 */
void imu_fifo_interrupt(Display_Handle display) {
    LOG_INF(display, 0, 0, "\nEnabling IMU interrupt for FIFO watermark level: %d", IMU_FIFO_WM);
    int status = enableFifoInterrupt(IMU_FIFO_WM, display);
    LOG_INF(display, 0, 0, "\nimu_fifo_interrupt error: %d\n", status);
}

/*
 * imu_reg_poll: polls data and adds it to the circular buffer
 */
void imu_reg_poll(Display_Handle display) {
    inv_imu_sensor_event_t imu_event;

    int error = getDataFromIMUReg(&imu_event, display);
    if (error) {
        LOG_INF(display, 0, 0, "\tgetDataFromIMUReg error: %d", error);
    }

   event_print(display, &imu_event);

    /* ADD TO CIRCULAR BUFFER */
//    bool added = 0;
    circular_buffer_add(imu_data_buffer, &imu_event);

//    if (!added) {
//        LOG_INF(display, 0, 0, "ERROR in adding to circular buffer. Probably full");
//    }
}

/*
 * get_fifo_data: reads data from the FIFO
 */
void get_fifo_data(Display_Handle display) {
    LOG_INF(display, 0, 0, "IMU FIFO retrieve");

    inv_imu_sensor_event_t imu_event;
    int fifo_status = getDataFromFifo(&imu_event, display);
    LOG_INF(display, 0, 0, "\tgot FIFO read status [%d] (0 is GOOD)", fifo_status);
    LOG_INF(display, 0, 0, "... done with IMU FIFO retrieve!");
}

/*
 * imu_get_temp: function to return temperature from IMU
 */
int16_t imu_get_temp(Display_Handle display) {
    int16_t imu_temp = getTempDataFromIMUReg();
    /* float temp_celsius = ((float)imu_temp / 128.0f) + 25.0f; */
    /* int16_t imu_c = (imu_temp / 128) + 25; */
    int16_t imu_f = ((imu_temp / 128) + 25) * 1.8 + 32;

    return imu_f;
}

/*
 * imu_get_pedo
 */
int imu_get_pedo(Display_Handle display) {
    float step_cadence = 0;
    const char* activity = 0;

    uint32_t count = 0;
    volatile int status = getPedometer(&count, step_cadence, activity); // TODO: figure out what the `step_cadence` variable is doing in this example? (same with activity?)
    step_count = count;

    if (status != -1) {
        step_count = step_count;
    }

    return step_count;
}


