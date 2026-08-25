/* standard C */
#include <string.h>

/* Zephyr files */
#include <zephyr/kernel.h>

/* My header files */
#include <nvs.h>
#include <imu.h>
#include <imu_bringup.h>
#include <ICM_42670.h>          /* getTempDataFromIMUReg() */
#include <util/cdc_debug.h>
#include <peripheral/clock.h>   /* get_dt_ticks() */
#include <peripheral/rv3028_bringup.h>
#include <peripheral/soc_temp.h>
#include <comm/rate_config.h>
#include <memory/nvs_bringup.h>

/* ===================== NVS / MT29F bring-up phase =========================
 *
 * First hardware with the NAND actually populated (SN3, 2026-07-26). Follows
 * the disciplined sequence from nvs_notes.md — one step per flash, advance the
 * macro between steps:
 *
 *   STEP_BASELINE  A: nvs_init() only, no records. Reboot 3-4x and watch
 *                     meta seq/write_addr — this is the clean-chip baseline the
 *                     open "META block drifts with zero writes" bug was
 *                     waiting for. seq must NOT advance across reboots.
 *   STEP_WRITE     B: erase chip, write a known count of tagged records,
 *                     flush + checkpoint metadata, dump. Count must match.
 *   STEP_RECOVER   C: nvs_init() only — write_addr must come back exactly
 *                     where B left it, and the dump must reproduce B's records.
 *   STEP_PIPELINE  D: the real thing. IMU first (0f89f1e order), then NVS;
 *                     FIFO records via imu_process(), temperature every 10 s,
 *                     TIME_ANCHOR at boot + every 5 min. Dummy RTC wall clock
 *                     (time_valid=0) until a battery is fitted.
 *
 * Every step re-checks IMU WHO_AM_I immediately after nvs_init() — the shared
 * SPI1 contention from the nRF52832 era ("NAND holds MISO, IMU reads 0x00")
 * has never been tested on this board and this is the first hardware that can.
 */
#define NVS_STEP_BASELINE   1
#define NVS_STEP_WRITE      2
#define NVS_STEP_RECOVER    3
#define NVS_STEP_PIPELINE   4
#define NVS_STEP_PLANE_TEST 5

#define NVS_BRINGUP_STEP  NVS_STEP_PIPELINE

/* Step B: how many tagged records to write. Payload raw values are 1000+i so
 * the dump is unmistakably this test and not stale flash content. */
#define NVS_TEST_RECORD_COUNT 25

#define ANCHOR_INTERVAL_SEC   (5 * 60)
/* Temperature interval and IMU ODR are runtime-configurable — see
 * src/comm/rate_config.h and CMD_GET_RATE/CMD_SET_RATE in protocol.c. */

static bool nvs_alive;

/* Dummy wall clock: RV-3028 runs but was never set (no backup battery), so
 * anchor date fields are fixed placeholders and time_valid=0. The h:m:s from
 * the RTC still go in — they show relative progression between anchors. */
static void nvs_log_boot_anchor(void)
{
    int secs = rv3028_seconds();
    struct record_time_anchor a = {
        .year = 2026, .month = 1, .day = 1,
        .hours = 0, .minutes = 0,
        .seconds = (secs >= 0) ? (uint8_t)secs : 0,
        .time_valid = 0,
    };
    int rc = nvs_log_time_anchor(a);

    cdc_printf("  TIME_ANCHOR logged -> %d (dummy clock, time_valid=0)\r\n", rc);
}

/* Direct reader for the 2040/2041 double-entry mystery: both blocks scanned as
 * valid seq=0 on the chip as soldered, which the current placement math cannot
 * produce in one lifetime (seq=0 -> block 2040 only). After step B's full-chip
 * erase, block 2041 page 0 must read 0xFF: if a valid entry ever reappears
 * there without seq reaching 64+, OUR write path is duplicating; if it stays
 * erased, the ghost was pre-existing data (chip history), case closed. */
static void meta_block_inspect(void)
{
    static uint8_t pg[2176];
    const mt29f_cfg_t *c = mt29f_get_config();
    uint32_t total_blocks = c->blocks_per_die * c->num_dies;

    cdc_write("  [meta inspect] page 0 of first two META blocks:\r\n");
    for (uint32_t b = total_blocks - META_BLOCK_COUNT;
         b < total_blocks - META_BLOCK_COUNT + 2; b++) {
        off_t addr = (off_t)b * c->pages_per_block * c->bytes_per_page;

        if (mt29f_read(addr, pg, sizeof(pg)) != 0) {
            cdc_printf("    block %u: READ FAILED\r\n", b);
            continue;
        }

        struct log_state st;
        memcpy(&st, pg, sizeof(st));
        cdc_printf("    block %u: magic=%08x seq=%u offset=%u crc=%08x\r\n",
                   b, st.magic, st.seq, (uint32_t)st.nand_offset, st.crc);
    }
}

/* Decisive test for the block 2040/2041 aliasing bug (see nvs_notes.md): write
 * DISTINCT data to one even block and one odd block, read both back, and
 * confirm they no longer alias now that spi_nand_page_cache_read() and
 * spi_nand_program_load() carry the plane-select bit (CA12) in the column
 * address. Runs against the last two data-region blocks so it does not touch
 * the META region. */
static void plane_alias_test(void)
{
    const mt29f_cfg_t *c = mt29f_get_config();
    const uint32_t bytes_per_block = (uint32_t)c->pages_per_block * c->bytes_per_page;
    const uint32_t total_blocks = c->blocks_per_die * c->num_dies;
    const uint32_t even_blk = total_blocks - META_BLOCK_COUNT - 2; /* even */
    const uint32_t odd_blk  = even_blk + 1;                        /* odd */

    static uint8_t pattern_even[2176];
    static uint8_t pattern_odd[2176];
    static uint8_t readback_even[2176];
    static uint8_t readback_odd[2176];

    memset(pattern_even, 0xAA, sizeof(pattern_even));
    memset(pattern_odd, 0x55, sizeof(pattern_odd));

    cdc_printf("\r\n[plane test] even blk=%u odd blk=%u\r\n", even_blk, odd_blk);

    mt29f_block_erase((off_t)even_blk * bytes_per_block);
    mt29f_block_erase((off_t)odd_blk * bytes_per_block);

    int rc_we = mt29f_write((off_t)even_blk * bytes_per_block, pattern_even, c->bytes_per_page);
    int rc_wo = mt29f_write((off_t)odd_blk * bytes_per_block, pattern_odd, c->bytes_per_page);
    cdc_printf("[plane test] write even rc=%d, write odd rc=%d\r\n", rc_we, rc_wo);

    int rc_re = mt29f_read((off_t)even_blk * bytes_per_block, readback_even, c->bytes_per_page);
    int rc_ro = mt29f_read((off_t)odd_blk * bytes_per_block, readback_odd, c->bytes_per_page);
    cdc_printf("[plane test] read even rc=%d, read odd rc=%d\r\n", rc_re, rc_ro);

    bool even_ok = memcmp(readback_even, pattern_even, c->bytes_per_page) == 0;
    bool odd_ok = memcmp(readback_odd, pattern_odd, c->bytes_per_page) == 0;
    bool cross_contaminated = memcmp(readback_odd, pattern_even, c->bytes_per_page) == 0;

    cdc_printf("[plane test] even block correct=%d (first byte=0x%02x)\r\n",
               even_ok, readback_even[0]);
    cdc_printf("[plane test] odd block correct=%d (first byte=0x%02x)\r\n",
               odd_ok, readback_odd[0]);
    cdc_printf("[plane test] odd block reads even pattern (ALIASING)=%d\r\n",
               cross_contaminated);
    cdc_printf("[plane test] RESULT: %s\r\n",
               (even_ok && odd_ok && !cross_contaminated)
                   ? "PASS -- plane fix confirmed, no aliasing"
                   : "FAIL -- aliasing still present");
}

void nvs_bringup_phase(void)
{
    /* The first ~1 s after DTR reliably loses CDC bytes to the host reader
     * settling — every capture so far dropped lines in exactly this window.
     * Cheap fix: let the phase output start after the window closes. */
    k_msleep(2000);

    cdc_printf("\r\n===== NVS / MT29F phase (step %d) =====\r\n", NVS_BRINGUP_STEP);

    nvs_init();
    nvs_alive = nvs_ready();
    cdc_printf("  nvs_init: %s  write_addr=%d  meta_seq=%u\r\n",
               nvs_alive ? "OK" : "FAILED",
               nvs_get_addr_offset(), nvs_get_metadata_seq());

    /* SPI1 coexistence check — the whole nRF52832-era contention question. */
    {
        int who = imu_raw_read_reg(ICM_WHO_AM_I_REG);

        cdc_printf("  IMU WHO_AM_I after nvs_init = 0x%02x (%s)\r\n", who & 0xff,
                   who == ICM_WHOAMI_EXPECT
                       ? "OK — no shared-bus contention"
                       : "CORRUPTED — 52832-era contention IS present here");
    }

    if (!nvs_alive) {
        cdc_write("=======================================\r\n");
        return;
    }

    meta_block_inspect();

    /* rate_config_init() (called earlier, before NVS was up) only set RAM
     * defaults — recover whatever the user last set via CMD_SET_RATE now
     * that the CONFIG region is reachable. */
    bool rate_restored = rate_config_load_persisted();
    cdc_printf("  rate config: %s (odr=%u Hz, temp_interval=%u s)\r\n",
               rate_restored ? "restored from flash" : "using defaults",
               rate_config_get_imu_odr_hz(), rate_config_get_temp_interval_sec());

#if NVS_BRINGUP_STEP == NVS_STEP_BASELINE
    /* Nothing else: no records, no erase. Reboot repeatedly and compare the
     * meta_seq/write_addr line above across boots. */
    cdc_write("  [baseline] no writes this boot — reboot and compare seq\r\n");

#elif NVS_BRINGUP_STEP == NVS_STEP_WRITE
    cdc_write("  [write test] erasing chip (takes a moment)...\r\n");
    nvs_erase_chip();

    for (int i = 0; i < NVS_TEST_RECORD_COUNT; i++) {
        struct record_temperature t = { .raw = (int16_t)(1000 + i) };
        int rc = nvs_log_record(RECORD_TEMPERATURE, &t, sizeof(t), get_dt_ticks());

        if (rc != 0) {
            cdc_printf("  [write test] record %d FAILED: %d\r\n", i, rc);
        }
    }

    nvs_flush_buffer();
    /* nvs_close() does NOT checkpoint metadata (CLAUDE.md is wrong there) —
     * checkpoint explicitly or step C recovers offset 0. */
    nvs_write_metadata(nvs_get_addr_offset());
    cdc_printf("  [write test] %d records flushed, write_addr=%d, meta_seq=%u\r\n",
               NVS_TEST_RECORD_COUNT, nvs_get_addr_offset(), nvs_get_metadata_seq());
    nvs_dump();

#elif NVS_BRINGUP_STEP == NVS_STEP_RECOVER
    /* nvs_init() above already did the recovery — expected: write_addr=2176
     * (one flushed page) and the dump reproduces step B's 25 tagged records. */
    cdc_write("  [recover] dump should show step B's records:\r\n");
    nvs_dump();

#elif NVS_BRINGUP_STEP == NVS_STEP_PIPELINE
    nvs_log_boot_anchor();
    cdc_printf("  [pipeline] logging: FIFO @ %u Hz + temp every %u s + anchor every %d s\r\n",
               rate_config_get_imu_odr_hz(), rate_config_get_temp_interval_sec(), ANCHOR_INTERVAL_SEC);

#elif NVS_BRINGUP_STEP == NVS_STEP_PLANE_TEST
    plane_alias_test();
#endif

    cdc_write("=======================================\r\n");
}

/* Called once per heartbeat second from the main loop (step D only). */
void nvs_pipeline_tick(void)
{
#if NVS_BRINGUP_STEP == NVS_STEP_PIPELINE
    static uint32_t seconds;

    if (!nvs_alive || !imu_alive) {
        return;
    }

    /* Drain the event circular buffer into NVS as RECORD_IMU_FIFO records
     * (gated by NVS_LOG_IMU_SAMPLES inside imu_process). */
    imu_process();

    seconds++;
    if (seconds % rate_config_get_temp_interval_sec() == 0) {
        int16_t raw = getTempDataFromIMUReg();
        struct record_temperature t = { .raw = raw };
        nvs_log_record(RECORD_TEMPERATURE, &t, sizeof(t), get_dt_ticks());
        temp_history_push(raw);  /* RAM copy for the graph screen, see imu.c */

        /* Logged on the same tick as the IMU temperature, immediately after,
         * so the two land adjacent in the log with the same dt_ticks. Pairing
         * them is the whole point — one die sensor cannot tell self-heating
         * from sensor error, two on the same board can (see soc_temp.h and
         * imu_notes.md). Logging them on separate cadences would force the
         * decoder to interpolate between mismatched timestamps for the
         * subtraction that matters. */
        int16_t soc_centi;
        if (soc_temp_read_centi_c(&soc_centi) == 0) {
            struct record_soc_temp s = { .centi_c = soc_centi };
            nvs_log_record(RECORD_SOC_TEMP, &s, sizeof(s), get_dt_ticks());
        }
    }
    if (seconds % ANCHOR_INTERVAL_SEC == 0) {
        nvs_log_boot_anchor();
    }
#endif
}
