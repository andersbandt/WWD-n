//*****************************************************************************
//!
//! @file config_store.c
//! @brief Persistent settings on the nRF52833's internal flash — see
//!        config_store.h for why this is not on the MT29F any more.
//!
//*****************************************************************************

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include "config_store.h"

LOG_MODULE_REGISTER(config_store, CONFIG_LOG_DEFAULT_LEVEL);

#define CONFIG_PARTITION FIXED_PARTITION_ID(storage_partition)

/* 0xFFFF is what erased NOR flash reads as, so it can never be a valid magic —
 * that is what makes "is this slot free?" a single comparison. */
#define CONFIG_MAGIC 0xCF01u

/* 8 bytes: two 32-bit words, which satisfies the nRF52 write-block-size of 4
 * without padding, and keeps every record naturally aligned. */
struct config_record {
    uint16_t magic;
    uint16_t imu_odr_hz;
    uint16_t temp_interval_sec;
    uint16_t crc;              /* crc16_ccitt over the first 6 bytes */
} __packed;

BUILD_ASSERT(sizeof(struct config_record) == 8,
             "record must stay 8 bytes: the slot arithmetic below assumes it");

/* Serialises the read-modify-write. rate_config_set_*() is reachable from the
 * protocol thread (CMD_SET_RATE) and, now that there is an on-device menu,
 * potentially from the UI thread as well. */
static K_MUTEX_DEFINE(config_mutex);

static uint16_t record_crc(const struct config_record *r)
{
    return crc16_ccitt(0, (const uint8_t *)r, sizeof(*r) - sizeof(r->crc));
}

static bool record_valid(const struct config_record *r)
{
    return r->magic == CONFIG_MAGIC && r->crc == record_crc(r);
}

/*
 * find_slots: walks the partition and reports where the newest valid record
 * lives and where the next one should be written.
 *
 * The log is append-only and contiguous, so the first free slot terminates the
 * scan — there is nothing valid beyond it.
 *
 * @param fa       open flash area
 * @param out_last offset of the newest valid record, or -1 if none
 * @param out_next offset of the first free slot, or -1 if the area is full
 */
static void find_slots(const struct flash_area *fa, off_t *out_last, off_t *out_next)
{
    struct config_record rec;
    size_t slots = fa->fa_size / sizeof(rec);

    *out_last = -1;
    *out_next = -1;

    for (size_t i = 0; i < slots; i++) {
        off_t off = (off_t)(i * sizeof(rec));

        if (flash_area_read(fa, off, &rec, sizeof(rec)) != 0) {
            LOG_ERR("config_store: read failed at 0x%lx", (long)off);
            return;
        }

        if (rec.magic == 0xFFFFu) {     /* erased => end of the log */
            *out_next = off;
            return;
        }

        if (record_valid(&rec)) {
            *out_last = off;
        } else {
            /* A torn or corrupted record. Skip it rather than stopping: the
             * scan is cheap, and treating it as the end of the log would
             * silently discard every good record written after it. */
            LOG_WRN("config_store: bad record at 0x%lx, skipping", (long)off);
        }
    }
    /* Fell off the end without finding a free slot: the area is full. */
}

int config_store_load(uint16_t *imu_odr_hz, uint16_t *temp_interval_sec)
{
    const struct flash_area *fa;
    struct config_record rec;
    off_t last, next;
    int rc;

    if (imu_odr_hz == NULL || temp_interval_sec == NULL) {
        return -EINVAL;
    }

    rc = flash_area_open(CONFIG_PARTITION, &fa);
    if (rc != 0) {
        LOG_ERR("config_store: flash_area_open failed: %d", rc);
        return rc;
    }

    k_mutex_lock(&config_mutex, K_FOREVER);
    find_slots(fa, &last, &next);

    if (last < 0) {
        rc = -ENOENT;
        goto out;
    }

    rc = flash_area_read(fa, last, &rec, sizeof(rec));
    if (rc == 0) {
        *imu_odr_hz        = rec.imu_odr_hz;
        *temp_interval_sec = rec.temp_interval_sec;
    }

out:
    k_mutex_unlock(&config_mutex);
    flash_area_close(fa);
    return rc;
}

int config_store_save(uint16_t imu_odr_hz, uint16_t temp_interval_sec)
{
    const struct flash_area *fa;
    struct config_record rec = {
        .magic             = CONFIG_MAGIC,
        .imu_odr_hz        = imu_odr_hz,
        .temp_interval_sec = temp_interval_sec,
    };
    struct config_record cur;
    off_t last, next;
    int rc;

    rec.crc = record_crc(&rec);

    rc = flash_area_open(CONFIG_PARTITION, &fa);
    if (rc != 0) {
        LOG_ERR("config_store: flash_area_open failed: %d", rc);
        return rc;
    }

    k_mutex_lock(&config_mutex, K_FOREVER);
    find_slots(fa, &last, &next);

    /* Skip the write if nothing changed. Settings are saved on every
     * CMD_SET_RATE, and a host that re-sends the same values (the GUI does on
     * connect) would otherwise burn a slot every time for no reason. */
    if (last >= 0 &&
        flash_area_read(fa, last, &cur, sizeof(cur)) == 0 &&
        cur.imu_odr_hz == imu_odr_hz &&
        cur.temp_interval_sec == temp_interval_sec) {
        rc = 0;
        goto out;
    }

    if (next < 0) {
        /* Full. Erase and restart from the top. The newest values are in `rec`
         * already, so nothing is lost -- only the history, which nothing
         * reads. */
        LOG_INF("config_store: partition full, erasing and restarting");
        rc = flash_area_erase(fa, 0, fa->fa_size);
        if (rc != 0) {
            LOG_ERR("config_store: erase failed: %d", rc);
            goto out;
        }
        next = 0;
    }

    rc = flash_area_write(fa, next, &rec, sizeof(rec));
    if (rc != 0) {
        LOG_ERR("config_store: write failed at 0x%lx: %d", (long)next, rc);
    } else {
        LOG_INF("config_store: saved odr=%u temp_interval=%u at 0x%lx",
                imu_odr_hz, temp_interval_sec, (long)next);
    }

out:
    k_mutex_unlock(&config_mutex);
    flash_area_close(fa);
    return rc;
}

int config_store_clear(void)
{
    const struct flash_area *fa;
    int rc = flash_area_open(CONFIG_PARTITION, &fa);

    if (rc != 0) {
        return rc;
    }

    k_mutex_lock(&config_mutex, K_FOREVER);
    rc = flash_area_erase(fa, 0, fa->fa_size);
    k_mutex_unlock(&config_mutex);

    flash_area_close(fa);
    LOG_INF("config_store: cleared (%d)", rc);
    return rc;
}
