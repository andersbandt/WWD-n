# IMU Notes

## [ONGOING] IMU Init Fails When NVS Is Enabled

`inv_imu_init()` returns `INV_ERROR_UNEXPECTED` (-12) when `nvs_init()` runs before `imu_init()`.
The -12 is set in `inv_imu_device_reset()` when `INT_STATUS` does not read back
`INT_STATUS_RESET_DONE_INT_MASK` after the soft reset.

**Observed behavior:**
- With `nvs_init()` enabled: IMU fails to init on every boot **except** the first boot after a full power cycle
- Comment out `nvs_init()`, flash, then **power cycle once**: IMU inits correctly on all subsequent boots
- Add `nvs_init()` back: works **once** (first boot after power cycle), then fails on every subsequent reset

**Root cause hypothesis:**
The MT29F NAND retains its internal state across MCU soft resets (it stays powered). The
first run of `mt29f_init()` against a freshly powered chip works fine. On the next MCU
reset, `mt29f_init()` runs again against an already-initialized chip — re-running the init
sequence (RESET → READ ID → SET FEATURE → UNLOCK) against a chip already in an operational
state likely leaves the MT29F's output driver in an active condition, driving MISO and
corrupting the IMU's SPI reads during its own init.

**Things ruled out:**
- CS GPIO misconfiguration — `cs-gpios` is correctly defined in the base overlay
- SPI mode mismatch — Zephyr reconfigures CPOL/CPHA per device before each transfer
- Soft reset timing in IMU driver — polling up to 10ms made no difference

**Additional observations:**
- Scoping the SPI clock during IMU init shows the clock appearing to "speed up" partway
  through the init sequence, even with both devices configured to the same `spi-max-frequency`.
  This is the `inv_imu_switch_on_mclk()` busy-poll loop — it fires back-to-back 1-byte
  `MCLK_RDY` register reads with minimal idle time between CS toggles, which looks visually
  faster than longer MT29F block transactions even at the same clock rate. This is normal
  behavior for MREG access during `init_hardware_from_ui()`.
- Reducing SPI frequency to 4 MHz changed the error from -12 (`INV_ERROR_UNEXPECTED`) at ~2s
  to -1 (`INV_ERROR` accumulated) at ~10s. This is progress: device reset now passes, but
  `inv_imu_switch_on_mclk()` is now timing out (~1s each × ~8 MREG accesses = ~8 extra seconds).
  The `MCLK_RDY` poll never sees the ready bit — SPI reads are still returning corrupt data.
  When `need_mclk_cnt > 0` on re-entry, `INV_ERROR` (-1 = 0xFFFFFFFF) gets OR'd into status,
  which dominates all other error bits, producing -1 as the final return value.

**Next steps to investigate:**
- Add a `READ STATUS` poll at the top of `mt29f_init()` — if OIP=0, chip is already
  initialized; consider skipping the full init sequence or issuing RESET first
- Send an explicit RESET command to the MT29F at the end of `nvs_close()` / on MCU reset
  to put it back in a known tri-stated condition before the MCU reboots
- Scope MISO (P0.8) on second boot with nvs_init enabled to confirm it is held driven
- Verify whether the "speedup" coincides exactly with the RESET_DONE_INT check failing —
  if so, MCLK polling may be creating SPI contention on shared bus lines

## APEX Mode: Every Other FIFO Packet Is Invalid

### Symptom
When `IMU_APEX_ENABLED` is set, the FIFO stream alternates between valid packets and
invalid packets where all accel values are `0x8000` (`INVALID_VALUE_FIFO` = -32768).
Pattern is perfectly regular: valid, invalid, valid, invalid.

### Root Cause
`startApex()` calls `inv_imu_enable_accel_low_power_mode()`, switching accel from LN to
LP+WUOSC mode. In this mode the ICM-42670 wakes the RCOSC on demand for each FIFO read.
The WU-OSC needs time to settle after wakeup — every other FIFO packet is captured during
the oscillator startup window and has invalid accel data. The driver correctly marks these
with `INVALID_VALUE_FIFO` in all three accel fields; the hardware is behaving as specified.

### Handling
`isAccelDataValid()` in `ICM_42670.c` filters these out by checking all three accel fields
against `INVALID_VALUE_FIFO`. Invalid packets are still passed to `event_cb` and enter the
circular buffer — they are silently dropped at the `event_print` / `imu_process` layer.
If storage efficiency matters, filter in `event_cb` before `circular_buffer_add`.

### WOM Disables FIFO_THS Interrupt (Related)
When WOM is enabled via `inv_imu_enable_wom()`, the ICM-42670 hardware **disables the
FIFO threshold interrupt** (`FIFO_THS`). This is a hardware behavior of the chip, not a
software configuration issue. `inv_imu_enable_wom()` does not touch `INT_SOURCE` registers
— the gating happens internally. Consequence: `INT2` (configured for `FIFO_THS`) goes
silent once WOM is active. The FIFO_FULL interrupt on INT2 is unaffected and may still
fire, but only when the buffer is completely full (~144 packets at 50Hz = ~2.9s latency).
Current workaround: drain FIFO from the INT1 (WOM) handler in `imu_thread_entry`.

## High-Resolution Mode (`IMU_HIGH_RES_ENABLED`)

The `inv_imu_sensor_event_t` struct always contains `accel_high_res[3]` and `gyro_high_res[3]`
because `inv_imu_driver.c` unconditionally writes to them whenever the FIFO reports a 20-bit
packet (`header->bits.twentybits_bit`). Gating the struct fields behind a `#define` breaks
the driver — don't do it.

`IMU_HIGH_RES_ENABLED` in `imu.h` should only gate whether `inv_imu_enable_high_resolution_fifo()`
is called during init — i.e., whether the hardware is *configured* for 20-bit mode. The struct
fields and driver parsing are always compiled in.

**To actually receive high-res data** you also need to configure the FIFO packet format on the
hardware side (20-bit packet mode) — that register configuration is not yet implemented.

## Event Buffer Heap Constraint

The IMU event buffer is allocated in `imu_init()`:
```c
circular_buffer_init(64, sizeof(inv_imu_sensor_event_t))
```

Total heap consumed = `64 * sizeof(inv_imu_sensor_event_t)` ≈ 1536 bytes.

`CONFIG_HEAP_MEM_POOL_SIZE=4096` (4 KB) in `prj.conf`. Buffer was previously 200 slots,
which overflowed the heap once `accel_high_res`/`gyro_high_res` were kept permanently in the
struct. 64 slots is sufficient — at 100 Hz with a FIFO watermark of 50, the buffer is drained
on every interrupt and never accumulates more than ~50 events.

If `circular_buffer_init` returns NULL (malloc fails silently), `circular_buffer_add` will
crash. Worth adding a NULL check on `imu_data_buffer` after init.

If slot count ever needs to increase, raise `CONFIG_HEAP_MEM_POOL_SIZE` in `prj.conf` first.

---

## SPI Bus Contention: NVS + IMU Init Failure

### Symptom
`imu_init()` fails with `INV_ERROR_UNEXPECTED` (-12) or `INV_ERROR` (-1) whenever `nvs_init()`
runs first. GDB confirmed `WHO_AM_I` reads back `0x00` (expected `0x67`) after NVS. Without
NVS, `WHO_AM_I` = `0x67` immediately.

### Root Cause
MT29F NAND (SPI Mode 3: CPOL=1, CPHA=1) and ICM-42670 (SPI Mode 0) share SPI1.
After `nvs_calc_offset()` completes its metadata page scan, the last operation is
`spi_nand_page_cache_read()`. When CS deasserts after a cache read, the MT29F does not
immediately tri-state MISO — it holds MISO driven (outputting cache data). This corrupts
the first SPI transaction to the IMU, which reads MISO as `0x00` for every byte.

Increasing `k_usleep` in `inv_imu_device_reset()` does **not** fix this — the issue is
physical bus contention, not a timing race in the IMU driver.

### Diagnostic Path (via `debug/gdb_query.sh`)
Broke at `inv_imu_device_reset` entry and called `readIMUReg(0x10075)` (WHO_AM_I):
- With NVS: `$1 = 0`, `imu_spi_rx_buf[1] = 0x00`
- Without NVS: `$1 = 103 (0x67)`, `imu_spi_rx_buf[1] = 0x67`

This ruled out timing, reset sequencing, and driver bugs. Pure hardware bus contention.

### Attempted Fixes (all unsuccessful)
- Increasing `k_usleep` in `inv_imu_device_reset()` — no effect, wrong layer
- `spi_nand_wait_until_ready()` at end of `mt29f_init()` — not sufficient, NVS does many
  page reads after init returns
- `spi_nand_wait_until_ready()` at end of `spi_nand_page_read()` after cache read — tried,
  did not resolve. CS pins confirmed high during infinite loop so it is not CS contention.

### Status
**UNRESOLVED as of 2026-03-17.** Root cause is confirmed (NAND holds MISO after NVS ops),
mechanism of why software fixes don't clear it is unknown. Likely needs a logic analyzer
to see what MISO is actually doing during the first IMU transaction. Consider running NVS
and IMU on separate SPI buses as a hardware workaround if software fix can't be found.

**Potential fix to investigate (2026-03-18):** Initializing the IMU *after* NVS with a
proper MT29F bus-release sequence (explicit RESET command to tri-state MISO before handing
the bus to the IMU) may resolve the contention. The current workaround is IMU-first with
NVS commented out — but if NAND can be forced to release MISO cleanly, NVS→IMU ordering
might work.

**Cleanup note:** The MT29F bus-release hackery added in commits `ebac1d2` and `b130a1e`
(wait_until_ready calls, SPI fixes attempting to force MISO tri-state) may be removable
under either of two conditions:
- (a) Init order fix works — IMU init after NVS succeeds once MT29F is properly reset first
- (b) Moot in hardware v2 — MT29F and ICM-42670 are planned for **separate SPI buses**,
  eliminating the shared-bus contention entirely. If v2 ships with separate buses, strip
  this code before bring-up to avoid masking any new issues.
