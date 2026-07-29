#ifndef IMU_BRINGUP_H
#define IMU_BRINGUP_H

#include <stdbool.h>
#include <stdint.h>

/* Set once imu_init() succeeds inside imu_probe(), so callers only read
 * sensor data from a part that is actually up. */
extern bool imu_alive;

#define ICM_WHO_AM_I_REG   0x75
#define ICM_WHOAMI_EXPECT  0x67

/* Single-byte register read, MSB set = read. Returns the byte, or -errno.
 * Exposed (not static) so nvs_bringup.c can re-check WHO_AM_I after NVS
 * operations for the shared-SPI1-bus coexistence check. */
int imu_raw_read_reg(uint8_t reg);

/* Full ICM-42670-P bring-up probe: raw WHO_AM_I read, NAND bus-health
 * control, register dumps, then imu_init() if the part is alive. Sets
 * imu_alive on success. */
void imu_probe(void);

#endif /* IMU_BRINGUP_H */
