# IMU Notes

## [2026-08-22] Raise-to-wake v1 implemented: WOM shares INT1 with FIFO_THS, no tilt yet

Follow-through on the RESEARCH note above and the TODO-sweep entry below it. First
prototype is on the wrist now (offline dev otherwise), so this is untested on hardware —
needs a real on-wrist check before trusting it.

**What's wired:**
- `enableFifoInterrupt()` (`ICM_42670.c`) now enables `INV_WOM_X/Y/Z` alongside
  `INV_FIFO_THS` in the `inv_imu_set_config_int1()` call that configures INT1 routing.
  WOM itself still gets armed later by `inv_imu_enable_wom()` in `startApex()` — routing
  it here just means it's live the instant that enable bit flips.
- New `checkWom()` (`ICM_42670.c`) / `imu_check_wom()` (`imu.c`, `USE_DERS_IMU`-gated like
  the rest of the driver) reads `INT_STATUS2` for the WOM bits. `INT_STATUS2` has no other
  reader anywhere in the driver, so this can't race the FIFO drain (`INT_STATUS`, owned by
  `inv_imu_get_data_from_fifo()`) or the pedometer/tilt poll (`INT_STATUS3`, owned by
  `updateApex()` on the 9s tick) — the whole reason tilt is NOT wired in this pass (see
  the TODO-sweep entry below).
- `button_handler_thread_entry()` (`main.c`), INT1 branch: after the existing
  `get_fifo_data()`/`imu_process()` FIFO drain, `if (imu_check_wom()) ui_wake_display_if_asleep();`.
- New `ui_wake_display_if_asleep()` (`ui.c`/`ui.h`) + a `wake_display_and_repaint()` static
  helper factored out of `handle_ui_input()`'s existing wake-on-button-press branch, so
  button wake and WOM wake now run the literal same repaint code. `handle_ui_input()`'s
  own lock/return structure is untouched — only its body was deduplicated — because that
  structure exists to fix a real GDB-diagnosed crash (button press landing mid-redraw).

**Two things considered and deliberately NOT done this pass:**
- *Enabling WOM's INT1 routing from `startApex()` instead of `enableFifoInterrupt()`.*
  `inv_imu_set_config_int1()` is a full overwrite of every source bit it manages on every
  call, not an incremental merge — a second call from `startApex()` with a WOM-only struct
  would silently clear `INV_FIFO_THS` (defaults to 0 in a fresh `{0}` struct) and kill FIFO
  routing, breaking NVS logging the moment `imu_apex()` ran. `enableFifoInterrupt()` stays
  the single place that declares the complete INT1 state.
- *Wiring the button interrupt directly to `ui_wake_display_if_asleep()`.* Buttons already
  wake the display inline in `handle_ui_input()`. Adding a separate call in main.c's button
  branches would run before `handle_ui_input()`'s own `!display_is_awake()` check, so that
  check would see the display as already-awake and fall through to also treat the same
  press as navigation — breaking the "first press after sleep only wakes, doesn't jump a
  menu position" contract. Also currently unreachable anyway: all four buttons are on the
  MCP23008 expander, not populated on this board.

**Still open (v2, if WOM-only produces too many false wakes on the wrist):** add the tilt
confirm-gate. Needs `int_status3` (the static accumulator in `ICM_42670.c`) protected by a
mutex once a second thread reads `INT_STATUS3`, or route the interrupt-thread's tilt check
through `updateApex()` itself rather than a second raw register read — see the TODO-sweep
entry below for the full race analysis.

## [RESOLVED 2026-07-26] IMU bring-up on nRF52833 (SN2): NFC pins killed the chip select

**Result: working.** `imu_init()` returns 0 and live accel data streams
(z ≈ 2112 at ±16 g FSR = 1.03 g flat, x/y ≈ 0). The driver code needed no changes —
it was already correct, as expected from the nRF52832 BETA board.

### Root cause
P0.09 and P0.10 are the nRF52833's **NFC antenna pins (NFC1/NFC2)** and boot in NFC
mode, not as GPIOs. This board wires both to the IMU: **P0.10 is the SPI chip select**
and P0.09 is INT1. With NFC mode active the CS pin could not be driven — it read stuck
low even against the internal pull-up — so the IMU was never selectable. `WHO_AM_I`
returned 0xff on every attempt.

`CLAUDE.md` records this fix being applied once before (for INT1), but it was lost when
the board was ported to `nrf52833_ders`. Restored in two places:

- `nrf52833_ders.dts`: `nfct-pins-as-gpios;` on `&uicr`
- `nrf52833_ders_defconfig`: `CONFIG_NFCT_PINS_AS_GPIOS=y`

Both are needed. The DTS property is the modern spelling but **on nRF52 it is not yet
acted on** — `dts/bindings/arm/nordic,nrf-uicr.yaml` accepts it, yet only
`soc/nordic/nrf54h/soc.c` reads it. For nRF52, `system_nrf52.c` still gates the
`UICR->NFCPINS` write on the (deprecated) Kconfig symbol. Setting only the DTS property
builds cleanly and silently does nothing. Written to UICR once on first boot; persists
until a UICR erase.

After the fix, `WHO_AM_I` = 0x67 and P0.10 reads free/healthy.

### Diagnostic that found it
A GPIO pull test on the SPI1 pins: configure each as input with the internal pull-up,
read, then with pull-down, read. A healthy free pin gives pu=1/pd=0. P0.10 gave
pu=0/pd=0 while SCK/MOSI/MISO were all healthy — an unmissable pointer to that one pin.

### Methodology warning — this test is destructive
That same pin test **rewrites `PIN_CNF`, handing SCK/MOSI/MISO to the GPIO block. SPIM
cannot drive them afterwards and every later SPI transaction returns garbage.**

Running it *before* `imu_init()` produced a long trail of false leads: `imu_init()`
failing -1/-12, reads degrading mid-sequence, and an apparent "any register write kills
the part" effect (a write of the value a register already held looked fatal). All of it
was the clobbered bus, not the IMU. Moving the pin test to after all SPI work made
`imu_init()` pass first try. **Only ever call it once SPI work is finished.**

### Ruled out along the way
- SPI clock speed — identical failure at 8 MHz and 1 MHz
- MT29F bus contention — **the NAND is not populated on this board**, so the IMU is the
  only possible driver of MISO and none of the shared-bus analysis below applies here
- Wrong 3-wire/4-wire mode select — `DEVICE_CONFIG` reads 0x04 (4-wire, mode 0/3) at
  power-up, exactly what `configure_serial_interface()` writes
- Stale MREG bank latch via `BLK_SEL_R`

### FIFO watermark interrupt on INT1 — working
`enableFifoInterrupt()` configured the FIFO and wrote the watermark, but **never routed
the watermark condition to a physical pin**, so it only ever set a bit in `INT_STATUS`
and no MCU interrupt could fire. `inv_imu_set_config_int1()` existed in the driver but
had no callers. The old board carried FIFO_THS on INT2; **INT2 is not wired on this
hardware**, so it has to go to INT1 (P0.09) here.

Added to `enableFifoInterrupt()`:
- `inv_imu_set_config_int1()` with only `INV_FIFO_THS` enabled → sets `INT_SOURCE0` bit 2
- `INT_CONFIG`: push-pull (no pull on this net), **active low** to match the DTS
  `int-gpios` `GPIO_ACTIVE_LOW`, and pulsed rather than latched so each crossing gives
  one clean edge instead of a level held until `INT_STATUS` is read

Verified: edge count tracks the FIFO drain rate — ~1/sec when draining once per second,
exactly 10/sec when draining at 10 Hz. That rate-tracking is what proves the edges are
watermark-driven rather than an artefact of the polling loop's own SPI traffic. Steady
state sits at 11-12 packets against `IMU_FIFO_WM` = 10.

**Re-arming (superseded 2026-07-31):** this originally read "`FIFO_CONFIG5.WM_GT_TH_EN`
is cleared, so the watermark fires only on `count == threshold` *exactly*" — which is
exactly the bug that made the FIFO produce zero records. `WM_GT_TH_EN` is now **set**
(>= threshold), so an undrained FIFO no longer sails past the threshold and goes silent
forever. The interrupt rate still tracks the drain rate in practice because the FIFO is
drained on every edge.

**FIFO count byte order:** 0x3d (named `FIFO_COUNTH` in the regmap) empirically holds the
**low** byte and 0x3e the high byte. Reading it the documented way yields an impossible
16384 for a 2 KB FIFO.

## [RESEARCH 2026-08-22] Raise-to-wake: what exists today

Asked "are the APEX features fully disabled, and where did FIFO sizing land?" —
answers, from reading the current tree (no hardware attached):

### APEX is fully ENABLED, but only one of its outputs is wired to a pin

`IMU_APEX_ENABLED = 1` (`imu.h`), so `imu_init()` calls `imu_apex()` ->
`startApex()` (`ICM_42670.c`), which currently:

- sets DMP ODR to 50 Hz (`APEX_CONFIG1_DMP_ODR_50Hz`), DMP power-save disabled
- **enables tilt detect** (`inv_imu_apex_enable_tilt`)
- **enables the pedometer** (`inv_imu_apex_enable_pedometer`)
- **configures and enables WOM** — thresholds 80/80/80, OR'd across axes,
  3-sample duration (`inv_imu_configure_wom` + `inv_imu_enable_wom`)

So the raise-to-wake building blocks (tilt + WOM) are already running in the
part. What is missing is a **route to the MCU**: `enableFifoInterrupt()` builds
an `inv_imu_interrupt_parameter_t` zeroed to all-off and turns on only
`INV_FIFO_THS` before calling `inv_imu_set_config_int1()`. INT1 (P0.09) is the
only interrupt line on this board (INT2 is not wired — `IMU_HAS_INT2` in
`interrupt.c`), and it is spoken for by the FIFO watermark, which is what feeds
the whole NVS logging pipeline. Tilt and WOM therefore only ever set bits in
`INT_STATUS3` / `INT_STATUS2`; nothing in firmware reads the tilt bit.

Note the ordering: `imu_fifo_interrupt()` runs *before* `imu_apex()` in
`imu_init()`, and `startApex()` never touches `INT_SOURCE0`, so the FIFO_THS
routing survives APEX bring-up.

The pedometer is the one APEX output actually consumed: `getPedometer()` polls
`INT_STATUS3` (`updateApex()`) on the 9 s `sensor_update_thread` tick and reads
`STEP_DET_INT` / `STEP_CNT_OVF_INT`. Same polling shape works for
`INT_STATUS3_TILT_DET_INT_MASK` (bit 3) — that is the cheapest raise-to-wake
path available without new hardware:

1. **Polled tilt (no wiring change).** Read `INT_STATUS3` on a tick and wake the
   display on `TILT_DET_INT`. Latency is bounded by the poll rate, and each poll
   is an SPI register read — the FIFO watermark interrupt already fires ~10x/s
   at 100 Hz/WM=10, so tilt could be checked from that same handler for free.
   Downside: tilt-detect is a "device orientation changed and held" detector, not
   a wrist-flick gesture — expect it to feel sluggish compared to a real
   raise-to-wake.
2. **Share INT1 between FIFO_THS and WOM/TILT.** Both can be enabled in
   `inv_imu_set_config_int1()`; the ISR would then have to read `INT_STATUS`
   /`INT_STATUS2`/`INT_STATUS3` to demux, which is extra SPI traffic in interrupt
   context on the bus the NAND and display also share.
3. **Wire INT2** — v2 hardware only; it is not routed on this PCB.

Also worth re-checking on hardware: the "[WOM Disables FIFO_THS Interrupt]"
claim further down this file was **disproved** on 2026-07-31 (datasheet +
driver code review) — WOM is enabled today and FIFO_THS demonstrably still
fires at ~100 Hz/WM=10, so that section is wrong and only kept for history.

### FIFO sizing — where it landed

- `IMU_FIFO_WM = 10` packets (`imu.h`), written to `FIFO_CONFIG2` by
  `enableFifoInterrupt()`. This overwrites the driver's own default of 1 that
  `inv_imu_configure_fifo()` writes.
- `FIFO_CONFIG5.WM_GT_TH_EN` is **set** (>= threshold), not the exact-equals
  comparison that caused the 0-records bug — set in both
  `inv_imu_configure_fifo()` and again explicitly in `enableFifoInterrupt()`.
- `INTF_CONFIG0`: FIFO count is in **records/packets**, little-endian
  (but see the "FIFO count byte order" note above — 0x3d empirically holds the
  low byte).
- `FIFO_CONFIG1`: **STREAM** mode, bypass off — i.e. on overflow the oldest data
  is overwritten rather than the FIFO latching up. (The comment above that code
  in `inv_imu_driver.c` still says "snapshot mode"; the snapshot line beneath it
  is commented out. The comment is stale, the code is stream.)
- Accel + gyro + FSYNC timestamp all enabled in `FIFO_CONFIG5`; hi-res off
  (`IMU_HIGH_RES_ENABLED = 0`).
- At 100 Hz with WM=10 that is an INT1 edge every ~100 ms; steady state sits at
  11-12 packets in the FIFO (see the FIFO watermark section above).

The host-side event buffer is separate and unchanged: `circular_buffer_init(64,
sizeof(inv_imu_sensor_event_t))`, sized against `CONFIG_HEAP_MEM_POOL_SIZE=4096`.

## [2026-08-22] TODO sweep: one real bug fixed, two design tradeoffs deferred on purpose

Working through the file-level TODOs left in the IMU driver (offline, no hardware
attached). One was a live bug; two were design questions that got documented rather
than "resolved" in code, because acting on them now would be a guess.

### Fixed: `getPedometer()`'s activity string was never wired up, and the call site had a real type bug
`ICM_42670.c`'s `getPedometer()` took `const char* activity` but never wrote through it —
the TODO said "I think I need some char variable containing activities if I want this to
return a string." Worse, the one call site (`imu_get_pedo()` in `imu.c`) declared
`const char* activity[20]` (an *array* of 20 uninitialized pointers) and passed that where
a single `const char*` was expected — a real signature mismatch that only compiled because
the parameter was dead on both ends.

Fixed properly:
- `getPedometer()`'s third param is now `const char **activity` (a real out-param, NULL-safe).
- Added `APEX_ACTIVITY_NAMES[4]` in `ICM_42670.c`, indexed by
  `APEX_DATA3_ACTIVITY_CLASS_t` (0=unknown, 1=walk, 2=run; index 3 unused/reserved but
  present so the `& APEX_DATA3_ACTIVITY_CLASS_MASK` guard can't run off the array).
- `imu_get_pedo()` now passes `&activity` (a real `const char *`), same "left untouched on
  a stale/absent read" contract as `step_count`/`step_cadence` already had.
- Nothing yet *displays* the activity string — `imu_get_pedo()` computes it but only
  returns `step_count`. Wiring it into the UI is a separate, later task.

### Deferred (documented, not implemented): WOM → interrupt routing
`inv_imu_disable_wom()`/`inv_imu_enable_wom()` in `inv_imu_driver.c` only ever touch
`WOM_CONFIG`'s enable bit in the sensor — they never route WOM onto an interrupt pin, and
a TODO wanted that spliced in. Traced where routing actually happens instead:
`inv_imu_set_config_int1()`/`int2()`, called from `init_hardware_from_ui()` and from
`enableFifoInterrupt()` in `ICM_42670.c` (see the RESEARCH note above) — and today
`enableFifoInterrupt()` writes a config that's all-off except `INV_FIFO_THS`, so WOM is
deliberately not on INT1.

Left it that way rather than wiring it in, because `imu_int1_handler()` (`interrupt.c`)
just gives a semaphore on every INT1 edge with no demux of `INT_STATUS`/`INT_STATUS2` —
whatever drains that semaphore today assumes "edge = FIFO watermark." Turning on
`INV_WOM_X/Y/Z` on INT1 without also adding that demux would just produce spurious
FIFO-drain wakeups with no consumer reading the WOM bit. This is really the same decision
as the raise-to-wake options listed above (specifically option 2, "share INT1 between
FIFO_THS and WOM/TILT") — so it should get decided once, there, not piecemeal via this
TODO. No code changed; the TODO is now a comment pointing here.

### Deferred (documented, not implemented): register-read caching
`inv_imu_read_reg()` had a TODO + commented-out sketch for reading from
`get_register_cache_addr()` (the 4-register shadow: `PWR_MGMT0`, `GYRO_CONFIG0`,
`ACCEL_CONFIG0`, `TMST_CONFIG1_MREG1`) instead of going to the bus. Checked every caller
in `inv_imu_driver.c` that touches those registers: all of them already call
`inv_imu_read_reg()` themselves immediately before using the value — none read the cached
field directly. So there's no live consumer to speed up today, and the cache is only kept
in sync by writes that go through `inv_imu_write_reg()` — any other write path, or the part
changing one of those bits on its own, would make a read-from-cache silently stale. Left
the read path as real bus reads; the reasoning is now a comment at the call site instead of
a bare TODO. Revisit only if profiling ever shows these specific reads are a bottleneck.

### Explained, not changed: why `imu_spi_write`/`imu_spi_read` take a `serif` they ignore
Just a documentation TODO ("why does this thing take in the serif?"). Both functions match
the fixed `write_reg`/`read_reg` function-pointer signature in `struct inv_imu_serif`
(`inv_imu_transport.h`) — InvenSense's vendor driver core calls through those pointers so
it's portable across MCUs/buses without modification. This board only ever has one IMU on
one hardcoded SPI device (`spi_dev`), so `serif` goes unused here; a port needing multiple
IMU instances or interface types would pull the device handle from `serif->context`
instead. Added `ARG_UNUSED(serif)` (matching the pattern already used elsewhere, e.g.
`imu_bringup.c`, `protocol.c`) plus the explanation as a comment.

---

## [STALE — does NOT reproduce on nRF52833] IMU Init Fails When NVS Is Enabled

**2026-07-26 (SN3, MT29F populated):** WHO_AM_I = 0x67 after `nvs_init()` on every
boot, including under continuous NAND write load. This section and the "SPI Bus
Contention" section below describe the old nRF52832 BETA board only. See
`src/memory/nvs_notes.md` for the SN3 session results — including the block-aliasing
bug that likely explains what was really happening back then.

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
