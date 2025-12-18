/**
 * @file      icm42670.h
 * @author    Taimur Islam Khan (taimurislamkhan@gmail.com)
 * 
 * @brief     Header file for ICM42670 6-axis IMU sensor driver interface.
 *            Provides functions and data structures for initializing and
 *            reading accelerometer and gyroscope data from the ICM42670
 *            sensor using Zephyr RTOS sensor driver framework.
 * 
 * @version   0.1
 * @date      25-01-2025
 * @copyright Copyright (c) 2025, Taimur Islam Khan
 */

#ifndef ICM42670_H
#define ICM42670_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

/** Structure to hold sensor data */
struct icm42670_data {
    double accel_x;    /**< Acceleration in X-axis (m/s^2) */
    double accel_y;    /**< Acceleration in Y-axis (m/s^2) */
    double accel_z;    /**< Acceleration in Z-axis (m/s^2) */
    double gyro_x;     /**< Angular velocity around X-axis (dps) */
    double gyro_y;     /**< Angular velocity around Y-axis (dps) */
    double gyro_z;     /**< Angular velocity around Z-axis (dps) */
};

/**
 * @brief Initialize the ICM42670 sensor
 * 
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_init(void);

/**
 * @brief Read both accelerometer and gyroscope data
 * 
 * @param data Pointer to structure where sensor data will be stored
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_read_all(struct icm42670_data *data);

/**
 * @brief Read only accelerometer data
 * 
 * @param data Pointer to structure where accelerometer data will be stored
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_read_accel(struct icm42670_data *data);

/**
 * @brief Read only gyroscope data
 * 
 * @param data Pointer to structure where gyroscope data will be stored
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_read_gyro(struct icm42670_data *data);

/**
 * @brief Set accelerometer sample rate
 * 
 * @param rate Sample rate in Hz (valid values: 1, 3, 6, 12, 25, 50, 100, 200, 400, 800, 1600)
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_set_accel_rate(uint16_t rate);

/**
 * @brief Set gyroscope sample rate
 * 
 * @param rate Sample rate in Hz (valid values: 12, 25, 50, 100, 200, 400, 800)
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_set_gyro_rate(uint16_t rate);

#endif /* ICM42670_H */
