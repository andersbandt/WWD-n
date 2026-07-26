# NVS / MT29F NAND Notes

Companion to `src/hardware/ic/imu/imu_notes.md`. Read this at the start of an NVS
bring-up session.

---

## [RESOLVED 2026-07-26] Block 2040/2041 plane-aliasing bug — FIXED and VERIFIED on SN3

**Fix applied:** `spi_nand_page_cache_read()` and `spi_nand_program_load()` in
`mt29f_nand.c` were always called with `col_addr = 0`, dropping the plane-select bit
(CA12, `COLUMN_PLANE_SELECT_POS` in `mt29f_defs.h`) that MT29F2G01's two-plane
architecture requires. Fixed at both call sites (`spi_nand_page_read()` and
`spi_nand_page_write()`) by computing `col_addr = (row_addr.blk_num & 0x1) <<
COLUMN_PLANE_SELECT_POS` and passing it through instead of the hardcoded `0`.

**Verification (direct GDB memory inspection, not log capture — see method note
below):**
1. Added a temporary `plane_alias_test()` step to the `main.c` bring-up harness
   (`NVS_STEP_PLANE_TEST`): erase two adjacent blocks (one even, one odd), write
   distinct fill patterns (0xAA / 0x55), read both back. Result: first 2048 bytes
   (the actual data region) of each block matched its own pattern exactly and did
   not cross-contaminate (`memcmp` over the data region == 0 for both, and the odd
   block did **not** read back the even block's pattern). The last 128 bytes (OOB/
   spare area) differed from the written pattern on **both** blocks in a
   block-specific way (distinct per block, not aliased) — consistent with
   controller-generated ECC parity in the spare area, unrelated to the plane bug.
   Do not mistake that OOB mismatch for a regression.
2. Reran the original repro directly: `NVS_STEP_WRITE` (single metadata checkpoint
   write to block 2040 page 0), then read block 2040 and block 2041 straight off
   flash via `mt29f_read()` called through GDB (bypassing the harness entirely).
   **Block 2040:** `magic=0xacacac seq=0 offset=2176 crc=0x87daa47f` (the write).
   **Block 2041:** `magic=0xffffffff seq=0xffffffff offset=0xff...ff crc=0xffffffff`
   — genuinely erased/blank NAND, not the ghost duplicate. This is the exact
   scenario that used to show byte-identical content at both addresses; it no
   longer does.
3. Confirmed no regression: rebuilt with `NVS_BRINGUP_STEP` back to
   `NVS_STEP_PIPELINE` (steady-state IMU+NVS logging) and reflashed. GDB breakpoint
   at `nvs_phase()` post-`nvs_init()` showed `nvs_alive=true`, `imu_alive=true`,
   metadata recovered cleanly from prior test data (`write_addr=43520 seq=20`) — no
   crash, no corruption.

**Method note — why GDB instead of serial log capture:** serial capture repeatedly
dropped the entire early-boot block (IMU init through `nvs_phase()`) on this session
even with the existing 2 s guard delay, apparently because a burst of log lines
right after DTR overflows something before the ring buffer starts reliably
recording — worse than the previously-documented "first ~1 s" drop. When log
capture is unreliable, prefer breaking with GDB and reading state/memory directly
(`p variable`, `x/NxN addr`, or `call some_function(...)` against static buffers by
address) over trusting `cdc_printf` output made it to the log file.

**Still open, not yet done:** step 4 of the original verification plan — drive
metadata rotation past `seq=64` so it physically writes into odd META blocks
(2041/2043/2045/2047) under the pipeline's normal checkpoint cadence (every 100
records) and confirm recovery survives the even→odd transition. This requires an
extended soak (hours, not the few minutes available this session) — the pipeline is
running now and will get there on its own if left alone; check `meta_seq` next
session and confirm it advances past 64 with correct recovery on reboot.

Not yet committed — see git status. `mt29f_defs.h`, `mt29f_nand.c` (the fix),
`main.c` (test harness for the above, currently left on `NVS_STEP_PIPELINE`) all
changed this session, on top of the prior uncommitted PM session state below.

---

## [SESSION 2026-07-26 PM] SN3 bring-up: pipeline WORKS, plane-aliasing bug FOUND

MT29F soldered onto SN3 today. The full disciplined sequence (steps A-D below) ran and
**passed**. All state below is from SN3, J-Link probe snr `801041202`, uncommitted
working tree (see "Uncommitted state" at the end of this section).

### Validated end-to-end (evidence on the wire, not assumption)

- **READ ID = 2c 24** on SN3. Chip alive, CS (P0.20) and MISO path good.
- **NO shared-SPI1 contention on this board.** `IMU WHO_AM_I after nvs_init = 0x67` on
  every single boot, including after full metadata scans and while the pipeline hammers
  the NAND ~1 page/s. The nRF52832-era "NAND holds MISO, IMU reads 0x00" bug **does not
  reproduce** on SN3. IMU-first-then-NVS init order retained anyway (0f89f1e order).
- **Baseline (step A):** metadata seq/write_addr stable across 3+ reboots with zero
  writes — the "META advances with 0 writes" drift does NOT happen per-boot. (What it
  actually was: see the aliasing bug below.)
- **Write test (step B):** `nvs_erase_chip()` = 2048 blocks in 6.1 s; 25 tagged
  TEMPERATURE records (raw=1000..1024) buffered, flushed (200 B), metadata
  checkpointed (seq=0, offset=2176, block 2040 page 0); `nvs_dump()` = 25/25 correct.
  NB: `nvs_close()` does NOT write metadata (CLAUDE.md wrong there) — step B
  checkpoints explicitly via `nvs_write_metadata(nvs_get_addr_offset())`.
- **Recovery (step C):** reboot → `write_addr=2176` recovered exactly, dump reproduced
  step B's records byte-for-byte. Two boots, identical result.
- **Pipeline (step D, running at handoff):** IMU FIFO → `imu_process()` →
  `RECORD_IMU_FIFO` at ~100 rec/s, page flush every ~1 s, metadata checkpoint every
  100 records marching through block 2040 pages 1,2,3,...,25+ with monotonic seq,
  boot TIME_ANCHOR logged (rc=0, dummy clock, time_valid=0), temp record every 10 s,
  anchor refresh every 300 s. Ran for minutes without error; P_FAIL/E_FAIL checks
  (new) never fired.

### [OPEN BUG — was being root-caused at handoff] Block 2040/2041 aliasing

**Observation:** after step B's FULL chip erase, exactly ONE metadata write was issued
(block 2040 page 0). Yet `meta_block_inspect()` (new helper in harness main.c) reads
**byte-identical content from block 2040 p0 AND block 2041 p0**:

```
block 2040: magic=00acacac seq=0 offset=2176 crc=87daa47f
block 2041: magic=00acacac seq=0 offset=2176 crc=87daa47f
```

One write, visible at two block addresses. This also retroactively explains the old
nRF52832 "META block advanced 2040→2041 with 0 writes" mystery — it was never a drift,
it was the same physical page read via two addresses.

**Leading hypothesis (UNVERIFIED — verify against the MT29F2G01 datasheet):** the die
is a **two-plane** device and **the plane-select bit is missing from the column
address**. On MT29F2G01, cache ops need the plane bit (typically **CA12** of the
column address) equal to `block & 1`:

- `PAGE READ (0x13)` with an odd-block row address loads that page into **plane 1's**
  cache register, but `spi_nand_page_cache_read()` always issues
  `READ FROM CACHE x1 (0x03)` with **col addr = 0** → it reads **plane 0's cache**,
  which still holds whatever even-block page was loaded last. Scan order reads 2040
  then 2041 → the 2041 read returns 2040's leftover cache. Exactly what we see.
- Same on the write side: `spi_nand_program_load()` sends col addr without the plane
  bit, so **programs targeting odd blocks likely load the wrong plane's cache** —
  odd-block writes may be silently corrupt (P_FAIL check added today won't catch a
  wrong-plane load that "succeeds").

**Why this matters a lot:** half the flash (every odd block) is potentially unreadable/
unwritable via the current driver, and metadata rotation will enter odd META blocks
(2041 = seq 64..127, 2043, 2045, 2047) where recovery would then read stale-but-CRC-valid
entries from the neighboring even block. That is a data-loss bug, not a cosmetic one.

**Where I was when interrupted:** just read `spi_nand_page_read()` /
`spi_nand_page_load()` / `spi_nand_page_cache_read()` (`mt29f_nand.c` ~line 300-380) and
confirmed row-address construction is symmetric read/write — so the row path is NOT the
bug, which strengthens the column-address/plane theory.

**Next actions, in order:**
1. Check the exact part number on SN3's silk/BOM and its datasheet section "READ FROM
   CACHE" / "PROGRAM LOAD" for the plane-select column-address bit (CA12 on 2-plane
   2Gb parts).
2. Decisive bench test (harness makes this easy): write a page of DISTINCT data to
   block 2041 p0 (odd) and block 2040 p0 (even), then read both back. Today's code
   will show cross-contamination; a fixed driver shows distinct data.
3. Fix: in `spi_nand_page_cache_read()` and `spi_nand_program_load()`, OR
   `(row_addr.blk_num & 1) << 12` into the column address (plumb blk_num through, or
   pass plane as an argument). Erase and PAGE READ/PROGRAM EXECUTE row addresses are
   fine as-is.
4. Re-run steps B/C/D afterwards, plus a targeted odd-block write/read/dump test, and
   let metadata rotation cross into block 2041 (write 64+ checkpoints) to prove
   recovery survives the even→odd block transition.

### Bench/workflow quirks discovered today (save yourself an hour)

- **The board cannot be power-cycled from the bench right now.** It is back-powered
  (J-Link VTref, most likely) — relay ch3 (micro-USB) off/on re-enumerates USB but the
  MCU keeps running (RAM counters keep counting). `nrfjprog --reset --snr 801041202`
  IS effective for reboots. `--pinreset` did nothing (nRESET likely not wired to the
  probe header). The JLinkARM "-256" error lines are noise; commands still execute.
- **Reboot + capture sequence that works:** `serial_disconnect` → `nrfjprog --reset`
  → poll `/dev/ttyACM*` (node alternates ACM0/ACM1 because the stale FD holds the old
  name) → `serial_connect` fast (firmware DTR window is 3 s + 1 s settle).
- **Early-boot CDC byte-drop:** the first ~1 s after DTR reliably loses output (every
  capture dropped lines in that window). The harness now sleeps 2 s at the top of
  `nvs_phase()` so the NVS block prints after the window — that's why step D's capture
  was finally complete.
- **Logs now ride USB CDC**: `prj.conf` has `CONFIG_LOG_BACKEND_UART=y` (console is
  `cdc_acm_uart0`), USB device/driver log levels cut DBG→ERR to avoid the
  log→USB→log feedback loop. RTT backend still enabled but unneeded.
- `nvs_dump()` of ~25 records is fine, but under pipeline load the log core drops
  messages ("--- N messages dropped ---") — dump counts are still authoritative from
  the `DUMP COMPLETE: n records` line.

### Uncommitted state (all of today's work, on `dev`, NOT committed)

- `src/main.c` — bring-up harness extended: `PROBE_NAND_CONTROL=1`; NVS phase state
  machine `NVS_BRINGUP_STEP` ∈ {BASELINE, WRITE, RECOVER, PIPELINE}, **currently
  PIPELINE**; `meta_block_inspect()`; `nvs_pipeline_tick()` (FIFO drain + temp/10 s +
  anchor/300 s); heartbeat prints `nvs addr=/seq=`.
- `src/memory/nvs.h` — `struct record_time_anchor` (raw_ticks + wall clock +
  time_valid); `nvs_log_record()` dt_ticks widened to uint32 (saturates to 0xFFFF on
  flash — harness never calls `get_raw_ticks()` so dt prints 65535, expected);
  `nvs_log_time_anchor()`, `nvs_get_metadata_seq()`, `nvs_ready()`.
- `src/memory/nvs.c` — implementations of the above, TIME_ANCHOR payload decode in
  `nvs_dump()`, `LOG_MODULE_REGISTER(..., CONFIG_LOG_DEFAULT_LEVEL)`.
- `src/memory/mt29f_nand.c` — **P_FAIL check** after program execute (returns -EIO),
  **E_FAIL check** after block erase (logs), log level → CONFIG_LOG_DEFAULT_LEVEL.
- `prj.conf` — log backend UART on, USB log levels ERR (comments preserved per rule).
- Build: `build_n33/` (pristine-configured today). Flash via
  `nrfjprog -f NRF52 --program build_n33/zephyr/zephyr.hex --sectorerase --verify --reset --snr 801041202`.

At handoff: relay ch3 ON, serial connected to `/dev/ttyACM0`, SN3 live and **actively
logging to NAND** (~2 KB/s, one META checkpoint/s — fine for hours, not weeks; power
down or reflash a non-pipeline step if leaving unattended).

---

## [RESOLVED 2026-07-26] ~~The MT29F is not populated on SN2 or SN3~~

**SN3 got its MT29F soldered on 2026-07-26 (PM) and it answers READ ID `2c 24` — see
the session section above. SN2 remains unpopulated.** Original note kept below:

Nothing below can be tested until a NAND is soldered on. Confirmed 2026-07-26 during IMU
bring-up: `mt29f` READ ID returns `ff ff` / `00 00` on an undriven bus, and the chip has
never responded on these boards.

This also means every "NAND holds MISO" conclusion in `imu_notes.md` was reached on the
**previous nRF52832 BETA board**, not on this hardware. See "Do not trust the old
contention analysis" below before spending time on it.

---

## Both flows you want already exist — this is validation, not new code

### Flow 1: IMU FIFO sample -> NVS write

Already wired end to end. Nothing to write:

- `imu_process()` in `src/hardware/ic/imu/imu.c` drains the event buffer and calls
  `nvs_log_record(RECORD_IMU_FIFO, &sample, sizeof(sample), get_dt_ticks())` at
  **imu.c:289**, gated by `#if NVS_LOG_IMU_SAMPLES` (`nvs.h`, currently **1**).
- `nvs_log_record()` (`nvs.c:533`) copies `struct log_entry_hdr` + payload into the static
  `page_buffer`, flushes via `nvs_flush_page_buffer()` -> `mt29f_write()` when the next
  record will not fit, and every **100 records** flushes plus rewrites metadata.

As of `d2cb035` the IMU half of this is proven working: `imu_init()` returns 0, accel data
is correct, and the FIFO watermark interrupt fires on INT1. So a failure in this flow is
an NVS/NAND problem, not an IMU one.

### Flow 2: recover the write offset at startup

Also already written: `nvs_init()` (`nvs.c:58`) -> `nvs_calc_offset()` (`nvs.c:349`) ->
`nvs_read_metadata()` (`nvs.c:253`).

Two-phase scan: phase 1 reads page 0 of all 8 META blocks and picks the highest valid
`seq` (magic `0xACACAC` + CRC32); phase 2 walks pages 1..N within that block until an
invalid page. `write_addr` is restored from the winning entry's `nand_offset`. No valid
metadata (fresh/erased flash) => start at 0 and write an initial entry.

`nvs_get_addr_offset()` (`nvs.c:380`) is the getter.

---

## The open bug that lands squarely on Flow 2

**Metadata block advanced 2040 -> 2041 within 2-3 reboots with zero calls to
`nvs_log_record()`.** Never conclusively root-caused; it was pinned specifically waiting
for a fresh NAND to give a clean baseline. Populating the chip *is* that experiment, so
run it deliberately and early:

1. Fresh chip, `nvs_erase_chip()`, then reboot 3-4 times with **no** logging at all.
2. Log the active META block and `seq` each boot.
3. Stable => the old chip's block 2040 was likely damaged, and the fix is to start
   checking PFAIL. Still jumping => it is a software bug in `nvs_read_metadata()`
   intermittently failing, causing `nvs_calc_offset()` to fall through to the
   "fresh flash" path and write a new entry every boot.

**`spi_nand_program_execute()` never checks the PFAIL bit** in `mt29f_nand.c`, so write
failures are currently silent. Worth adding regardless of which way the above lands — a
silent write failure is indistinguishable from a logic bug right now.

Already fixed previously, do not re-investigate: the CRC bug (`sizeof(state) -
sizeof(state.crc)` vs `offsetof(struct log_state, crc)` — the struct has 4 bytes trailing
padding, so the CRC covered its own field), metadata using all pages per block (8x64 =
512 slots), and the public `mt29f_block_erase()`.

---

## Do not trust the old shared-SPI1 contention analysis

`imu_notes.md` documents "IMU init fails when NVS runs first" as unresolved bus
contention. Re-test it from scratch rather than building on those conclusions:

1. **It was diagnosed on the nRF52832 BETA board**, whose NFC pins are **also P0.09 and
   P0.10** — the exact pins that turned out to be the IMU's chip select and INT1, and
   which silently broke everything on this board until `d2cb035`. Some of that old log's
   CS observations ("CS pins confirmed high") may have been measuring a pin that was not
   a GPIO at all.
2. This session proved how easily this specific area gets misdiagnosed: a long run of
   "IMU init fails / any register write kills the part" results were entirely an artefact
   of a diagnostic that reconfigured `PIN_CNF` on the SPI pins. See the methodology
   warning in `imu_notes.md`.

When the NAND goes on, the honest starting position is: unknown whether IMU and NVS can
share SPI1 on this board. Test it directly. Note the MT29F is SPI **Mode 3** and the IMU
is **Mode 0** on the same bus, which is real and unchanged.

Init order in the real `main()` (commit `0f89f1e`) is **IMU first, then NVS**, with the
comment "the init order of these might matter ... couldn't get IMU to init properly when
it was after". Keep that order for the first attempt.

v2 hardware plans to split these onto separate SPI buses, at which point the bus-release
hackery from commits `ebac1d2` / `b130a1e` should be stripped rather than carried forward.

---

## Things that will bite during bring-up

- **`src/main.c` is currently a bring-up harness**, not the application. The real `main()`
  with the LED/button/display/thread setup and the NVS block is at commit `0f89f1e`.
- **`CLAUDE.md` is stale** where it says `nvs_init()` is commented out in `main.c` — at
  `0f89f1e` it is active (line ~311), immediately followed by `nvs_dump()`.
- **Recovery granularity.** Metadata checkpoints every 100 records, so a power cut loses
  up to 100 records plus whatever is in the page buffer. If recovery fidelity is the point
  of the exercise, lower that interval (`nvs.c:590`) and accept more META wear.
- **`nvs_dump()` only shows flushed pages.** The in-memory page buffer is invisible. Call
  `nvs_flush_buffer()` first, or you will think records went missing.
- **Log levels are hardcoded**, against the project's own rule: `nvs.c:30` and
  `mt29f_nand.c:35` both use `LOG_LEVEL_INF`, so `CONFIG_LOG_DEFAULT_LEVEL` in `prj.conf`
  will not raise them to DBG. Change them to `CONFIG_LOG_DEFAULT_LEVEL` if you need debug
  output.
- **Logs currently go to RTT, not USB.** `prj.conf` has `CONFIG_LOG_BACKEND_UART=n` +
  RTT for the USB bring-up. USB CDC is proven working now, so restoring the UART backend
  is reasonable — the commented-out line is preserved in `prj.conf` for exactly this.
  RTT was not readable via `JLinkRTTLogger` this session (control block not found), so
  budget for switching to the CDC console rather than relying on RTT.

---

## Suggested order

1. Solder the NAND. Confirm `mt29f` READ ID returns `2c 24` before anything else — the
   IMU probe scaffolding in the harness `main.c` has a ready-made `nand_id_probe()`
   behind `PROBE_NAND_CONTROL`.
2. Fresh-chip metadata stability test (above), with logging disabled.
3. `nvs_erase_chip()` -> log a known small number of records -> `nvs_flush_buffer()` ->
   `nvs_dump()` and check the count matches.
4. Power-cycle and confirm `nvs_calc_offset()` recovers the expected `write_addr`.
5. Only then enable `NVS_LOG_IMU_SAMPLES` with the IMU running, and re-test IMU+NVS
   coexistence on the shared bus from scratch.
