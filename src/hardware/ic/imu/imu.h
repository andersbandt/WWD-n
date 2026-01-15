//*****************************************************************************
//!
//! @file imu.h
//! @author Anders Bandt
//! @brief Function definitions for IMU control. Will try to keep code IC independent
//! @version 0.9
//! @date November 2023
//!
//*****************************************************************************

#ifndef SRC_IC_IMU_IMU_H_
#define SRC_IC_IMU_IMU_H_


/* My header files  */
#include <circular_buffer.h>

/* Display driver selection */
// #define USE_ZEPHYR_IMU
#define USE_DERS_IMU

#ifdef USE_DERS_IMU
    #include <inv_imu_driver.h>
#endif



// FIFO configuration
#define IMU_FIFO_ENABLED     1
#define IMU_APEX_ENABLED     0
#define IMU_FIFO_WM          50


extern Circular_Buffer *imu_data_buffer;

extern volatile uint32_t step_count;


/**
 * @brief function for initialization the IMU
 *
 * @return init status indictaor
 */
int imu_init();


/**
 * @brief function for starting accelerometer and gyrometer of IMU
 *
 * @return initialization status indicator
 */
int imu_start();


/**
 * @brief function for enabling APEX functionality
 */
int imu_apex();


/**
 * @brief returns the IMU event at the top ? of the buffer stack
 */
inv_imu_sensor_event_t imu_deque();


/**
 * @brief processes from the IMU receive buffer
 */
void imu_process();


/**
 * @brief function for polling the register data of the IMU
 */
void imu_reg_poll();


/**
 * @brief function for just getting temperature data from IMU
 */
int16_t imu_get_temp();


/**
 * @brief function for enabling the FIFO interrupt for the IMU
 */
int imu_fifo_interrupt();


/**
 * @brief retrieves the data from the FIFO
 */
void get_fifo_data();


/**
 * @brief prints out pedometer info from the IMU
 */
int imu_get_pedo();


#endif /* SRC_IC_IMU_IMU_H_ */
