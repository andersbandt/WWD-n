# NVS / MT29F NAND Notes

Companion to `src/hardware/ic/imu/imu_notes.md`. Read this at the start of an NVS
bring-up session.

---

## [BLOCKER] The MT29F is not populated on SN2 or SN3

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
