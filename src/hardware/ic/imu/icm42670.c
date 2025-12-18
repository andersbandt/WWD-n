/**
 * @file      icm42670.c
 * @author    Taimur Islam Khan (taimurislamkhan@gmail.com)
 * 
 * @brief     Implementation of ICM42670 6-axis IMU sensor driver interface.
 *            This module provides functions for reading accelerometer and
 *            gyroscope data from the ICM42670 sensor. The sensor communicates
 *            over SPI and uses the Zephyr sensor driver framework. The driver
 *            supports configurable sample rates and can read acceleration and
 *            angular velocity data in all three axes.
 * 
 * @version   0.1
 * @date      25-01-2025
 * @copyright Copyright (c) 2025, Taimur Islam Khan
 */

#include "icm42670.h"
#include <stdio.h>

/** Static device pointer for the ICM42670 sensor */
static const struct device *icm42670_dev;

/**
 * @brief Initialize the ICM42670 sensor driver
 * 
 * This function initializes the ICM42670 sensor driver by getting the device
 * binding and checking if the device is ready.
 * 
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_init(void)
{
    /* Get device binding */
    icm42670_dev = DEVICE_DT_GET_ONE(invensense_icm42670p);

    if (icm42670_dev == NULL) {
        printk("Error: Device not found in devicetree\n");
        return -ENODEV;
    }

    if (!device_is_ready(icm42670_dev)) {
        printk("Error: Device not ready\n");
        return -ENODEV;
    }

    printk("ICM42670 initialized successfully\n");
    return 0;
}

/**
 * @brief Helper function to fetch latest sensor data
 * 
 * This function fetches the latest sensor data from the ICM42670 sensor.
 * 
 * @return int 0 on success, negative errno code on failure
 */
static int fetch_sensor_data(void)
{
    int ret = sensor_sample_fetch(icm42670_dev);
    if (ret < 0) {
        printk("Error: Failed to fetch sensor data: %d\n", ret);
        return ret;
    }
    return 0;
}

/**
 * @brief Read all sensor data (acceleration and gyroscope)
 * 
 * This function reads all sensor data (acceleration and gyroscope) from the
 * ICM42670 sensor and stores it in the provided data structure.
 * 
 * @param data Pointer to the data structure to store the sensor data
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_read_all(struct icm42670_data *data)
{
    int ret;
    struct sensor_value accel[3], gyro[3];

    /* Fetch sensor data */
    ret = fetch_sensor_data();
    if (ret < 0) {
        return ret;
    }

    /* Get acceleration data */
    ret = sensor_channel_get(icm42670_dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (ret < 0) {
        printk("Error: Failed to get acceleration data: %d\n", ret);
        return ret;
    }

    /* Get gyroscope data */
    ret = sensor_channel_get(icm42670_dev, SENSOR_CHAN_GYRO_XYZ, gyro);
    if (ret < 0) {
        printk("Error: Failed to get gyroscope data: %d\n", ret);
        return ret;
    }

    /* Convert to double and store in data structure */
    data->accel_x = sensor_value_to_double(&accel[0]);
    data->accel_y = sensor_value_to_double(&accel[1]);
    data->accel_z = sensor_value_to_double(&accel[2]);
    data->gyro_x = sensor_value_to_double(&gyro[0]);
    data->gyro_y = sensor_value_to_double(&gyro[1]);
    data->gyro_z = sensor_value_to_double(&gyro[2]);

    return 0;
}

/**
 * @brief Read acceleration data from the ICM42670 sensor
 * 
 * This function reads acceleration data from the ICM42670 sensor and stores it
 * in the provided data structure.
 * 
 * @param data Pointer to the data structure to store the acceleration data
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_read_accel(struct icm42670_data *data)
{
    int ret;
    struct sensor_value accel[3];

    /* Fetch sensor data */
    ret = fetch_sensor_data();
    if (ret < 0) {
        return ret;
    }

    /* Get acceleration data */
    ret = sensor_channel_get(icm42670_dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (ret < 0) {
        printk("Error: Failed to get acceleration data: %d\n", ret);
        return ret;
    }

    /* Convert to double and store in data structure */
    data->accel_x = sensor_value_to_double(&accel[0]);
    data->accel_y = sensor_value_to_double(&accel[1]);
    data->accel_z = sensor_value_to_double(&accel[2]);

    return 0;
}

/**
 * @brief Read gyroscope data from the ICM42670 sensor
 * 
 * This function reads gyroscope data from the ICM42670 sensor and stores it in
 * the provided data structure.
 * 
 * @param data Pointer to the data structure to store the gyroscope data
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_read_gyro(struct icm42670_data *data)
{
    int ret;
    struct sensor_value gyro[3];

    /* Fetch sensor data */
    ret = fetch_sensor_data();
    if (ret < 0) {
        return ret;
    }

    /* Get gyroscope data */
    ret = sensor_channel_get(icm42670_dev, SENSOR_CHAN_GYRO_XYZ, gyro);
    if (ret < 0) {
        printk("Error: Failed to get gyroscope data: %d\n", ret);
        return ret;
    }

    /* Convert to double and store in data structure */
    data->gyro_x = sensor_value_to_double(&gyro[0]);
    data->gyro_y = sensor_value_to_double(&gyro[1]);
    data->gyro_z = sensor_value_to_double(&gyro[2]);

    return 0;
}

/**
 * @brief Set the accelerometer sampling rate
 * 
 * This function sets the accelerometer sampling rate of the ICM42670 sensor.
 * 
 * @param rate The desired sampling rate
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_set_accel_rate(uint16_t rate)
{
    struct sensor_value odr = {.val1 = rate, .val2 = 0};
    int ret = sensor_attr_set(icm42670_dev, SENSOR_CHAN_ACCEL_XYZ,
                            SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
    if (ret != 0) {
        printk("Error: Failed to set accelerometer sampling rate: %d\n", ret);
        return ret;
    }
    return 0;
}

/**
 * @brief Set the gyroscope sampling rate
 * 
 * This function sets the gyroscope sampling rate of the ICM42670 sensor.
 * 
 * @param rate The desired sampling rate
 * @return int 0 on success, negative errno code on failure
 */
int icm42670_set_gyro_rate(uint16_t rate)
{
    struct sensor_value odr = {.val1 = rate, .val2 = 0};
    int ret = sensor_attr_set(icm42670_dev, SENSOR_CHAN_GYRO_XYZ,
                            SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
    if (ret != 0) {
        printk("Error: Failed to set gyroscope sampling rate: %d\n", ret);
        return ret;
    }
    return 0;
}
