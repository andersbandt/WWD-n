//*****************************************************************************
//!
//! @file icm_42670.h
//! @author Anders Bandt
//! @brief Anders' IMU (ICM_42670) control file
//! @version 0.9
//! @date November 2023
//!
//*****************************************************************************


#include <stdint.h>
#include <stdbool.h>


#include <inv_imu_driver.h>


// This defines the handler called when retrieving a sample from the FIFO
typedef void (*ICM42670P_sensor_event_cb)(inv_imu_sensor_event_t *event);

// This defines the handler called when receiving an irq
typedef void (*ICM42670P_irq_handler)(void);


/* functions for converting freq/FSR parameters to register values */
ACCEL_CONFIG0_ODR_t accel_freq_to_param(uint16_t accel_freq_hz);
GYRO_CONFIG0_ODR_t gyro_freq_to_param(uint16_t gyro_freq_hz);
ACCEL_CONFIG0_FS_SEL_t accel_fsr_g_to_param(uint16_t accel_fsr_g);
GYRO_CONFIG0_FS_SEL_t gyro_fsr_dps_to_param(uint16_t gyro_fsr_dps);


/* functions for declaring how the IMU conducts SPI reads and writes */
int imu_spi_write(struct inv_imu_serif *serif, uint8_t reg, const uint8_t *buf, uint32_t len);
int imu_spi_read(struct inv_imu_serif *serif, uint8_t reg, uint8_t *buf, uint32_t len);



/**
 * @brief prints out an IMU event
 */
void event_print(inv_imu_sensor_event_t *evt);


/**
 * @brief unsure exactly how this thing is working. something with memcpy
 */
void event_cb(inv_imu_sensor_event_t *evt);


/**
 * @brief initializes the IMU
 */
int init_icm();


/**
 * @brief starts the accelerometer in the ICM-42670
 *
 * @param[in] odr measured in Hz. Typical value is 100?
 *
 * @param[in] fsr Full-scale range. Typical value is 16G
 */
int startAccel(uint16_t odr, uint16_t fsr);


/**
 * @brief starts the gyrometer in the ICM-42670
 *
 * @param[in] odr measured in Hz. Typical value is 100?
 *
 * @param[in] fsr Full-scale range. Typical value is 16G
 */
int startGyro(uint16_t odr, uint16_t fsr);


/**
 * @brief starts the APEX (advanced motion features of IMU)
 */
int startApex();


/**
 * @brief gets data from the IMU temp register
 */
int16_t getTempDataFromIMUReg();


/**
 * @brief reads the raw PWR_MGMT0 byte (accel/gyro power mode, IDLE).
 * @return 0 on success, negative on error
 */
int getPwrMgmt0(uint8_t *out);


/**
 * @brief gets data from the IMU registers
 */
int getDataFromIMUReg(inv_imu_sensor_event_t* evt);


/**
 * @brief configures the interrupt for the FIFO
 */
int enableFifoInterrupt(uint8_t fifo_watermark);


/**
 * @brief displays the current IMU interrupt configuratoin
 */
void checkInterruptIMU();


/**
 * @brief gets the current FIFO count in the buffer
 */
void getFifoCount();


/**
 * @brief gets IMU data from the FIFO
 */
int getDataFromFifo(void);


/**
 * @brief raise-to-wake v1: true if a WOM event fired on any axis since the
 * last call (see checkWom() in ICM_42670.c for why this is safe to share
 * INT1 with the FIFO watermark)
 */
bool checkWom(void);


/*
 * @brief gets the current step count recorded by the IMU
 *
 * @param[out] activity  On a fresh step-detect event, *activity is set to point at a
 *                        static string naming the APEX activity class ("unknown"/"walk"/
 *                        "run" — see APEX_ACTIVITY_NAMES in ICM_42670.c). Pass NULL if the
 *                        caller doesn't need it. Left untouched when there's no fresh event
 *                        (same "no write on stale read" behavior as step_count/step_cadence).
 */
int getPedometer(uint32_t * step_count, float * step_cadence, const char **activity);


// ICM42670P C++ class functions
bool isAccelDataValid(inv_imu_sensor_event_t *evt);

bool isGyroDataValid(inv_imu_sensor_event_t *evt);




