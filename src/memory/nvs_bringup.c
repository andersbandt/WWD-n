/* standard C */
#include <string.h>

/* Zephyr files */
#include <zephyr/kernel.h>

/* My header files */
#include <nvs.h>
#include <mt29f_nand.h>
#include "mt29f_defs.h"
#include <imu.h>
#include <imu_bringup.h>
#include <ICM_42670.h>          /* getTempDataFromIMUReg() */
#include <util/cdc_debug.h>
#include <peripheral/clock.h>   /* get_dt_ticks() */
#include <peripheral/rv3028.h>       /* rv3028_time_is_set() */
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
 *                     TIME_ANCHOR at boot + every 5 min, carrying the real
 *                     RV-3028 wall clock (time_valid=1 once the clock is set).
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

/* Set to 1 to run the one-shot MT29F page-layout / ECC probe at boot
 * (src/memory/nand_page_defects.md). Destructive to the two scratch blocks it
 * uses and, via the plane test, to the CONFIG blocks. Diagnostics only — turn
 * back off once the defects are settled. */
#define NVS_PAGE_PROBE 0

/* Step B: how many tagged records to write. Payload raw values are 1000+i so
 * the dump is unmistakably this test and not stale flash content. */
#define NVS_TEST_RECORD_COUNT 25

#define ANCHOR_INTERVAL_SEC   (5 * 60)
/* Temperature interval and IMU ODR are runtime-configurable — see
 * src/comm/rate_config.h and CMD_GET_RATE/CMD_SET_RATE in protocol.c. */

static bool nvs_alive;

/* T1 mirror — plane_alias_test() runs long before probe_results is declared, so
 * it parks its verdict here and page_layout_test() copies it across. */
static uint32_t plane_even_ok_mirror;
static uint32_t plane_odd_ok_mirror;
static uint32_t plane_first_mismatch_mirror;

/* Real wall clock, as of 2026-08-25.
 *
 * This used to hardcode placeholder date fields and time_valid=0, from when
 * the RV-3028 ran but had never been set. It works now (LSM backup mode, keeps
 * time across power cycles), and get_current_time()/get_date() already read it
 * directly — so every dump was needlessly relative-time-only. That stops
 * mattering in the abstract and starts mattering concretely the moment
 * activity sessions exist and a run has to be tied to a time of day.
 *
 * time_valid comes from rv3028_time_is_set(), so a board whose clock was never
 * set still logs 0 and decoders still fall back to relative time. Read that
 * function's comment for what "valid" does NOT promise: it means the fields
 * came from a real set RTC, not that the RTC is set to the RIGHT time. */
/* Mirror of the last anchor written, readable over SWD — same rationale as
 * battery_dbg in power.c: the cdc_printf() below goes nowhere with USB
 * unplugged, and USB has to stay unplugged for battery measurements. */
volatile struct {
    uint32_t seq;
    uint16_t year;
    uint8_t  month, day, hours, minutes, seconds;
    uint8_t  time_valid;
    int32_t  rc;
} anchor_dbg;

static void nvs_log_boot_anchor(void)
{
    Time t = get_current_time();
    Date d = get_date();
    bool valid = rv3028_time_is_set();

    struct record_time_anchor a = {
        .year    = d.year,
        .month   = d.month,
        .day     = d.day,
        .hours   = (uint8_t)t.hours,
        .minutes = (uint8_t)t.minutes,
        .seconds = (uint8_t)t.seconds,
        .time_valid = valid ? 1 : 0,
    };
    int rc = nvs_log_time_anchor(a);

    anchor_dbg.year       = a.year;
    anchor_dbg.month      = a.month;
    anchor_dbg.day        = a.day;
    anchor_dbg.hours      = a.hours;
    anchor_dbg.minutes    = a.minutes;
    anchor_dbg.seconds    = a.seconds;
    anchor_dbg.time_valid = a.time_valid;
    anchor_dbg.rc         = rc;
    anchor_dbg.seq++;

    cdc_printf("  TIME_ANCHOR logged -> %d (%04u-%02u-%02u %02u:%02u:%02u, time_valid=%d)\r\n",
               rc, d.year, d.month, d.day, t.hours, t.minutes, t.seconds, a.time_valid);
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

    /* Compare only the usable page. This test used to memcmp all 2176 bytes and
     * so always failed at byte 2112 on the die's ECC parity, reporting
     * "aliasing still present" on a chip with no aliasing at all — the plane
     * fix has been fine since 2026-07-26. See src/memory/nand_page_defects.md. */
    const uint16_t cmp_len = c->usable_bytes_per_page;
    bool even_ok = memcmp(readback_even, pattern_even, cmp_len) == 0;
    bool odd_ok = memcmp(readback_odd, pattern_odd, cmp_len) == 0;
    bool cross_contaminated = memcmp(readback_odd, pattern_even, cmp_len) == 0;

    plane_even_ok_mirror = even_ok ? 1u : 0u;
    plane_odd_ok_mirror = odd_ok ? 1u : 0u;
    plane_first_mismatch_mirror = c->bytes_per_page;
    for (uint16_t i = 0; i < cmp_len; i++) {
        if (readback_even[i] != pattern_even[i]) {
            plane_first_mismatch_mirror = i;
            break;
        }
    }
    cdc_printf("[plane test] even first mismatch at byte %u\r\n",
               plane_first_mismatch_mirror);

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


/* ===================== T1/T2/T3/T4 page-layout probe =======================
 *
 * Settles the two defects written up in src/memory/nand_page_defects.md:
 *
 *  A  the chip owns bytes 2048..2175 of every page once internal ECC is on, so
 *     the last 128 bytes of every page_buffer are silently discarded;
 *  B  pages are being programmed twice without an erase (bits only ever clear).
 *
 * Writes a non-0xFF pattern to a scratch page, reads it back, and reports where
 * the round-trip stops being faithful — with ECC enabled and again with it
 * disabled, which is the A/B that pins the cause on the ECC engine rather than
 * on the driver's addressing. Scratch blocks are the last two DATA blocks,
 * below CONFIG_BLOCK_START, so neither the CONFIG nor the META region is
 * touched. Destructive to those two blocks only.
 */
#define PROBE_PAGE_BYTES 2176

/* SWD-readable mirror of the probe's results. The CDC console only exists for
 * a ~2 s window after enumeration and the probe output kept landing in it
 * before any host could attach, so every number the probe produces is also
 * parked here — read it with
 *   nrfjprog --memrd $(nm -C build_n33/zephyr/zephyr.elf | grep probe_results)
 * See project_swd_ram_mirror_diagnostics. Field order is fixed; append only. */
struct probe_result_page {
    uint32_t magic;          /* 0x50524F42 'PROB' once this arm has run */
    int32_t  rc_write;
    int32_t  rc_read;
    uint32_t status;         /* STATUS byte sampled after the read */
    uint32_t first_mismatch; /* PROBE_PAGE_BYTES if the whole page round-tripped */
    uint32_t main_match;     /* bytes 0..2047   read back as written */
    uint32_t main_other;
    uint32_t spare_match;    /* bytes 2048..2111 */
    uint32_t spare_erased;
    uint32_t parity_match;   /* bytes 2112..2175 */
    uint32_t parity_erased;
    uint8_t  tail[48];       /* readback bytes 2040..2087 */
};

struct probe_results {
    uint32_t magic;
    uint32_t cfg_reg_at_entry;
    uint32_t resume_addr;
    uint32_t resume_page_erased;   /* 1 = safe, 0 = live data (defect B) */
    uint8_t  resume_first16[16];
    uint32_t plane_even_ok;
    uint32_t plane_odd_ok;
    uint32_t plane_first_mismatch;
    struct probe_result_page ecc_on;
    struct probe_result_page ecc_off;
    uint32_t ecc_events;

    /* T5: how far behind the true end of the log the recovered offset is. */
    uint32_t scan_pages_live;      /* live pages found from resume_addr onward */
    uint32_t scan_first_erased;    /* page index of the first erased page */
    uint32_t scan_capped;          /* 1 = hit the scan limit, distance is a floor */

    /* T6: two pages the 2026-08-26 dump decoded as corrupt, re-read from the
     * chip with ECC status now wired up. */
    uint32_t t6_page[2];
    uint32_t t6_status[2];
    uint32_t t6_first_bad_len[2];  /* offset of the first length field that is
                                    * not 0x000e on the 20-byte grid, or 0 */
    uint8_t  t6_head[2][32];

    uint32_t done_magic;
};

volatile struct probe_results probe_results;

static uint8_t probe_pattern[PROBE_PAGE_BYTES];
static uint8_t probe_readback[PROBE_PAGE_BYTES];

/* Never 0xFF, so "discarded" (reads back erased) is distinguishable from
 * "written". 251 is prime, so the sequence does not align with any power-of-two
 * page/sector boundary either. */
static void probe_fill_pattern(uint8_t salt)
{
    for (size_t i = 0; i < PROBE_PAGE_BYTES; i++) {
        probe_pattern[i] = (uint8_t)((i + salt) % 251u);
    }
}

static void probe_hexdump(const char *tag, const uint8_t *buf, size_t from, size_t to)
{
    for (size_t base = from; base < to; base += 16) {
        char line[64];
        int n = 0;

        for (size_t i = base; i < base + 16 && i < to; i++) {
            n += snprintk(&line[n], sizeof(line) - n, "%02x ", buf[i]);
        }
        cdc_printf("    %s %4u: %s\r\n", tag, (unsigned)base, line);
    }
}

/* Reports, for one region, how many bytes came back as written / erased / other. */
static void probe_region_report(const char *name, size_t from, size_t to)
{
    size_t match = 0, erased = 0, other = 0;

    for (size_t i = from; i < to; i++) {
        if (probe_readback[i] == probe_pattern[i]) {
            match++;
        } else if (probe_readback[i] == 0xFF) {
            erased++;
        } else {
            other++;
        }
    }

    cdc_printf("    %-14s [%4u..%4u]  match=%u erased=%u other=%u\r\n",
               name, (unsigned)from, (unsigned)to - 1,
               (unsigned)match, (unsigned)erased, (unsigned)other);
}

static void probe_one_page(const char *label, uint32_t blk, uint8_t salt,
                           volatile struct probe_result_page *out)
{
    const mt29f_cfg_t *c = mt29f_get_config();
    const uint32_t bytes_per_block = (uint32_t)c->pages_per_block * c->bytes_per_page;
    const off_t addr = (off_t)blk * bytes_per_block;

    probe_fill_pattern(salt);
    memset(probe_readback, 0x00, sizeof(probe_readback));

    mt29f_block_erase(addr);

    int rc_w = mt29f_write(addr, probe_pattern, c->bytes_per_page);
    int rc_r = mt29f_read(addr, probe_readback, c->bytes_per_page);
    uint8_t status = mt29f_last_read_status();

    cdc_printf("  [%s] blk=%u write rc=%d read rc=%d  status=0x%02x eccs=0x%x\r\n",
               label, blk, rc_w, rc_r, status, mt29f_ecc_status_of(status));

    size_t first_bad = PROBE_PAGE_BYTES;

    for (size_t i = 0; i < PROBE_PAGE_BYTES; i++) {
        if (probe_readback[i] != probe_pattern[i]) {
            first_bad = i;
            break;
        }
    }

    if (first_bad == PROBE_PAGE_BYTES) {
        cdc_printf("    all %u bytes round-tripped\r\n", PROBE_PAGE_BYTES);
    } else {
        cdc_printf("    first mismatch at byte %u (wrote 0x%02x, read 0x%02x)\r\n",
                   (unsigned)first_bad, probe_pattern[first_bad], probe_readback[first_bad]);
    }

    probe_region_report("main", 0, 2048);
    probe_region_report("user spare", 2048, 2112);
    probe_region_report("parity", 2112, 2176);
    probe_hexdump(label, probe_readback, 2032, 2064);
    probe_hexdump(label, probe_readback, 2096, 2176);

    if (out) {
        memset((void *)out, 0, sizeof(*out));
        out->rc_write = rc_w;
        out->rc_read = rc_r;
        out->status = status;
        out->first_mismatch = (uint32_t)first_bad;

        for (size_t i = 0; i < 2048; i++) {
            if (probe_readback[i] == probe_pattern[i]) {
                out->main_match++;
            } else if (probe_readback[i] != 0xFF) {
                out->main_other++;
            }
        }
        for (size_t i = 2048; i < 2112; i++) {
            if (probe_readback[i] == probe_pattern[i]) {
                out->spare_match++;
            } else if (probe_readback[i] == 0xFF) {
                out->spare_erased++;
            }
        }
        for (size_t i = 2112; i < 2176; i++) {
            if (probe_readback[i] == probe_pattern[i]) {
                out->parity_match++;
            } else if (probe_readback[i] == 0xFF) {
                out->parity_erased++;
            }
        }
        memcpy((void *)out->tail, &probe_readback[2040], sizeof(out->tail));
        out->magic = 0x50524F42;
    }
}

/* T4: is the offset that metadata recovery handed us pointing at a LIVE page?
 * If so the next flush programs it a second time without an erase, which is
 * defect B's suspected mechanism. Read-only. */
static void probe_resume_offset(void)
{
    const mt29f_cfg_t *c = mt29f_get_config();
    const off_t addr = (off_t)nvs_get_addr_offset();

    if (mt29f_read(addr, probe_readback, c->bytes_per_page) != 0) {
        cdc_printf("  [T4] read of resume page at %ld FAILED\r\n", (long)addr);
        return;
    }

    bool erased = true;

    for (size_t i = 0; i < c->bytes_per_page; i++) {
        if (probe_readback[i] != 0xFF) {
            erased = false;
            break;
        }
    }

    probe_results.resume_addr = (uint32_t)addr;
    probe_results.resume_page_erased = erased ? 1u : 0u;
    memcpy((void *)probe_results.resume_first16, probe_readback,
           sizeof(probe_results.resume_first16));

    cdc_printf("  [T4] resume page @%ld: %s  first16: ", (long)addr,
               erased ? "ERASED (safe)" : "*** LIVE DATA — next write double-programs it ***");
    for (size_t i = 0; i < 16; i++) {
        cdc_printf("%02x ", probe_readback[i]);
    }
    cdc_printf("\r\n");
}


/* T5: walk forward from the recovered write_addr to the first erased page. The
 * distance is exactly how many already-written pages the next writes would
 * program a second time. Read-only; capped so a bad recovery cannot hang boot. */
static void probe_scan_forward(void)
{
    const mt29f_cfg_t *c = mt29f_get_config();
    const uint32_t limit = 1024;
    off_t addr = (off_t)nvs_get_addr_offset();
    uint32_t live = 0;

    while (live < limit) {
        if (mt29f_read(addr, probe_readback, c->bytes_per_page) != 0) {
            break;
        }

        bool erased = true;

        /* A header of six 0xFF bytes is the same test nvs_dump() uses to find
         * the end of a page's records — enough to call a page unwritten. */
        for (size_t i = 0; i < 6; i++) {
            if (probe_readback[i] != 0xFF) {
                erased = false;
                break;
            }
        }
        if (erased) {
            break;
        }

        live++;
        addr += c->bytes_per_page;
        /* Published every iteration so an SWD read can watch it advance. */
        probe_results.scan_pages_live = live;
    }

    probe_results.scan_pages_live = live;
    probe_results.scan_first_erased = (uint32_t)(addr / c->bytes_per_page);
    probe_results.scan_capped = (live >= limit) ? 1u : 0u;

    cdc_printf("  [T5] %u live pages from the resume offset; first erased page %u%s\r\n",
               live, probe_results.scan_first_erased, (live >= limit) ? " (CAPPED)" : "");
}

/* T6: re-read two pages the host-side decode called corrupt, straight off the
 * chip, and report what the ECC engine says about them. Confirms the corruption
 * is on flash rather than a dump-transport artifact. */
static void probe_known_bad_pages(void)
{
    const mt29f_cfg_t *c = mt29f_get_config();
    static const uint32_t pages[2] = { 81638u, 94658u };

    for (int k = 0; k < 2; k++) {
        const off_t addr = (off_t)pages[k] * c->bytes_per_page;

        probe_results.t6_page[k] = pages[k];

        if (mt29f_read(addr, probe_readback, c->bytes_per_page) != 0) {
            cdc_printf("  [T6] page %u: READ FAILED\r\n", pages[k]);
            continue;
        }

        probe_results.t6_status[k] = mt29f_last_read_status();
        memcpy((void *)probe_results.t6_head[k], probe_readback,
               sizeof(probe_results.t6_head[k]));

        /* First length field on the 20-byte IMU_FIFO grid that is not 14. */
        for (uint16_t off = 0; off + 4 < 2040; off += 20) {
            uint16_t len = (uint16_t)(probe_readback[off + 2] |
                                      (probe_readback[off + 3] << 8));

            if (len != 0x000E) {
                probe_results.t6_first_bad_len[k] = off;
                break;
            }
        }

        cdc_printf("  [T6] page %u: status=0x%02x eccs=0x%x first bad len @%u\r\n",
                   pages[k], probe_results.t6_status[k],
                   mt29f_ecc_status_of((uint8_t)probe_results.t6_status[k]),
                   probe_results.t6_first_bad_len[k]);
    }
}

static void page_layout_test(void)
{
    const mt29f_cfg_t *c = mt29f_get_config();
    const uint32_t total_blocks = (uint32_t)c->blocks_per_die * c->num_dies;
    /* Last two DATA blocks: CONFIG_BLOCK_START is total-META-CONFIG, so
     * total-META-CONFIG-1 and -2 are the highest blocks the log itself uses. */
    const uint32_t blk_ecc_on  = total_blocks - META_BLOCK_COUNT - CONFIG_BLOCK_COUNT - 1;
    const uint32_t blk_ecc_off = blk_ecc_on - 1;
    uint8_t cfg_reg = 0;

    cdc_write("\r\n===== page layout probe (nand_page_defects.md) =====\r\n");

    memset((void *)&probe_results, 0, sizeof(probe_results));
    probe_results.magic = 0x50524F42;

    mt29f_get_feature(REG_CONFIGURATION, &cfg_reg);
    probe_results.cfg_reg_at_entry = cfg_reg;
    cdc_printf("  CONFIGURATION reg = 0x%02x (ECC_EN=%d)\r\n",
               cfg_reg, (cfg_reg & SEC_STATUS_BIT_ECC_EN) ? 1 : 0);

    probe_results.plane_even_ok = plane_even_ok_mirror;
    probe_results.plane_odd_ok = plane_odd_ok_mirror;
    probe_results.plane_first_mismatch = plane_first_mismatch_mirror;

    probe_resume_offset();
    probe_scan_forward();
    probe_known_bad_pages();

    /* T2: as shipped, ECC enabled. */
    probe_one_page("T2 ecc-on", blk_ecc_on, 0, &probe_results.ecc_on);

    /* T2b: same test with the ECC engine off. If all 2176 bytes round-trip
     * here and only here, the spare is chip-owned under ECC and defect A is
     * proven; if the tail is lost either way, the cause is elsewhere. */
    if (mt29f_set_feature(REG_CONFIGURATION, (uint8_t)(cfg_reg & ~SEC_STATUS_BIT_ECC_EN)) == 0) {
        uint8_t check = 0;

        mt29f_get_feature(REG_CONFIGURATION, &check);
        cdc_printf("  CONFIGURATION reg now 0x%02x (ECC_EN=%d)\r\n",
                   check, (check & SEC_STATUS_BIT_ECC_EN) ? 1 : 0);

        probe_one_page("T2b ecc-off", blk_ecc_off, 7, &probe_results.ecc_off);

        mt29f_set_feature(REG_CONFIGURATION, cfg_reg);
        mt29f_get_feature(REG_CONFIGURATION, &check);
        cdc_printf("  CONFIGURATION restored to 0x%02x\r\n", check);
    } else {
        cdc_write("  could not clear ECC_EN — skipping the ecc-off arm\r\n");
    }

    probe_results.ecc_events = mt29f_ecc_event_count();
    probe_results.done_magic = 0x444F4E45; /* 'DONE' */
    cdc_printf("  ECC events since boot: %u (last offset %ld)\r\n",
               mt29f_ecc_event_count(), (long)mt29f_ecc_last_offset());
    cdc_write("=====================================================\r\n");
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

#if NVS_PAGE_PROBE
    /* T1: the existing plane test memcmps all 2176 bytes of an 0xAA page, so
     * under defect A it must fail at byte 2112. If it passes, defect A is
     * wrong and nothing below should be believed. Note it erases the two
     * blocks at CONFIG_BLOCK_START, i.e. it destroys the saved rate config. */
    plane_alias_test();
    page_layout_test();
#endif

    /* Rate config used to be RESTORED here, from the NAND's CONFIG blocks —
     * which meant it could only ever be restored on a board whose NAND worked,
     * since everything above this point is behind `if (!nvs_alive) return`.
     * It now lives on the nRF's internal flash and is loaded from main.c,
     * unconditionally. Reported here only because this is where the boot
     * summary is printed. */
    cdc_printf("  rate config: odr=%u Hz, temp_interval=%u s\r\n",
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
        /* The temp-history ring used to be fed from HERE, which quietly made
         * two unrelated things depend on each other: this whole function
         * returns early unless `nvs_alive && imu_alive`, so on any board where
         * the NAND is absent or the wrong part (SN1) the graph and the temp
         * list screens had NO data at all and just showed "0". A display ring
         * has no business being gated on flash logging. It is now pushed from
         * sensor_update_thread in main.c, which runs regardless of NVS and
         * already holds both temperatures. */

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
