#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>

/*
 * Small persistent settings store on the nRF52833's OWN flash.
 *
 * Why not the MT29F, where this used to live:
 *
 *  1. A chip erase took the settings with it. CMD_ERASE and the new on-device
 *     Erase Flash screen both wipe the NAND, and losing your sample rates as a
 *     side effect of clearing the log is a genuine surprise -- you erase to
 *     free space, not to reset the device.
 *  2. It made settings depend on hardware that is not always there. The MT29F
 *     is unpopulated on SN2 and is the wrong part on SN1, so on those boards
 *     rate settings could not persist at all.
 *  3. It is 4 bytes of configuration sitting behind a 2 Gbit NAND, a bus shared
 *     with the IMU and the display, and two mutexes.
 *
 * So the NAND is now data only, which is what it is good at.
 *
 * Storage is the `storage_partition` already declared in the board DTS (24 KB
 * at 0x7a000, previously unused). Deliberately NOT Zephyr's NVS subsystem:
 * this project has its own nvs_*() API for the NAND logger, and pulling in
 * <zephyr/fs/nvs.h> alongside it invites exactly the kind of name confusion
 * that costs an afternoon. flash_area is a thin enough layer that an
 * append-only record log is a hundred lines.
 */

/**
 * @brief Persist the sample-rate settings.
 *
 * Appends a new record; the partition is erased and restarted only when it
 * fills, which at 8 bytes a record is every 3072 saves.
 *
 * @return 0 on success, negative errno on failure
 */
int config_store_save(uint16_t imu_odr_hz, uint16_t temp_interval_sec);

/**
 * @brief Recover the most recently saved settings.
 *
 * @return 0 and fills the out-params if a valid record was found;
 *         -ENOENT if the store is empty (fresh device), leaving them untouched;
 *         negative errno on a flash error.
 */
int config_store_load(uint16_t *imu_odr_hz, uint16_t *temp_interval_sec);

/**
 * @brief Erase every saved setting, returning the device to its defaults.
 *
 * Not called by the flash-erase paths on purpose -- clearing the log and
 * resetting your settings are different intentions, and keeping them separate
 * is the whole reason this store exists. Here for a future explicit
 * "factory reset".
 */
int config_store_clear(void);

#endif /* CONFIG_STORE_H */
