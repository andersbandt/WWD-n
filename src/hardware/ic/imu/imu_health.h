#ifndef IMU_HEALTH_H
#define IMU_HEALTH_H

#include <stdint.h>
#include <stdbool.h>

/*
 * IMU data-path stall detection and recovery.
 *
 * The fault this exists for: on 2026-08-28 the ICM-42670's sensor data path
 * stopped dead at 08:34 and stayed stopped for 6h09m until a power cycle.
 * Throughout, SPI register reads kept SUCCEEDING and returning plausible
 * values — the IMU temperature register returned 28.6875 degC, bit-identical,
 * 2199 times in a row. WHO_AM_I would have read 0x67 the whole time. See
 * imu_notes.md for the full investigation.
 *
 * So the detector cannot be "does the part answer SPI". It has to be "is the
 * data path still running", and the cheapest witness to that is TEMP_DATA:
 * it is refreshed by the internal data path at ODR, sensor_update_thread
 * already reads it every 9 s for the clock face, and at 128 LSB/degC a live
 * sensor essentially never repeats a raw value exactly. In the same dump a
 * healthy boot produced 537 distinct raw values where the stalled one
 * produced 1.
 */

/* Consecutive identical raw temperature readings that count as a stall.
 * 6 ticks x the 9 s sensor_update period is ~54 s — long enough that a
 * genuinely static reading can't trip it, short enough that a stall costs
 * about a minute of logging rather than six hours. */
#define IMU_HEALTH_FROZEN_TICKS 6

/* Minimum spacing between recovery attempts. A part that is properly dead
 * would otherwise be reset every ~54 s forever, which burns power and floods
 * the log with the same record — the same failure mode as the full-NAND
 * error spam. */
#define IMU_HEALTH_RECOVERY_MIN_INTERVAL_MS (5 * 60 * 1000)

/**
 * @brief Feed one raw temperature reading to the stall detector. Call once
 *        per sensor_update tick, with the same raw register value that goes
 *        into RECORD_TEMPERATURE — raw, not converted, so the comparison is
 *        exact rather than at float precision.
 *
 * On tripping it reads PWR_MGMT0 (which says whether ACCEL_MODE/GYRO_MODE are
 * still on), writes a RECORD_IMU_HEALTH, and attempts recovery.
 */
void imu_health_tick(int16_t raw_temp);

/**
 * @brief Soft-reset the IMU and rebuild its whole configuration, without a
 *        power cycle. The ICM-42670-P has no reset pin (its LGA has none, and
 *        the DTS node carries only int-gpios), and the part sits on the
 *        board's permanent 3.3 V rail, so this is the only lever there is.
 *
 * Takes the IMU bus lock for the entire sequence: a reset run underneath a
 * concurrent FIFO drain is exactly the unsynchronised-access hazard that is
 * the leading suspect for the original fault.
 *
 * @return 0 on success, negative on failure (the part is left as it was).
 */
int imu_recover(void);

/** @brief true while the detector considers the data path stalled. */
bool imu_health_is_stalled(void);

/** @brief number of recovery attempts made since boot. */
uint32_t imu_health_recovery_count(void);

#endif /* IMU_HEALTH_H */
