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

#include <stdint.h>

/* My header files  */
#include <circular_buffer.h>

/* Display driver selection */
// NOTE: these are done in the CMakeLists.txt file (USE_DERS_IMU and USE_ZEPHYR_IMU)

#ifdef USE_DERS_IMU
    #include <inv_imu_driver.h>
#endif


// FIFO configuration
#define IMU_FIFO_ENABLED     1
#define IMU_APEX_ENABLED     1
#define IMU_FIFO_WM          10

#define IMU_HIGH_RES_ENABLED 0   /* see imu_notes.md before enabling */


struct imu_sample {
    uint32_t step_count;
    int16_t temperature;
};


extern Circular_Buffer *imu_data_buffer;

extern uint32_t step_count;
extern int16_t imu_temperature;


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
 * @brief runtime ODR change for both accel and gyro, at the FSR imu_start()
 *        already configured them with (16 g / 2000 dps — not adjustable
 *        here, see startAccel()/startGyro() in ICM_42670.c for FSR control).
 *        Valid values: 25/50/100/200/400/800 Hz (ICM-42670 discrete steps).
 *
 * @return 0 on success, -EINVAL for an unsupported hz value
 */
int imu_set_odr(uint16_t odr_hz);


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
float imu_get_temp();


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


/**
 * @brief prints out pedometer info from the IMU
 */
int imu_log();


#endif /* SRC_IC_IMU_IMU_H_ */
