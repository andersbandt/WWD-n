# IMU Notes (ICM-42670P)

## ODR (Output Data Rate)

**FIXED 2026-07-30** (was: `startApex()` silently overwrote accel to 50Hz/LP
mode regardless of what `startAccel()` set — see git history for the old
version of this section if needed).

The accel ODR is set in `imu_start()` → `startAccel(100, ...)` (`imu.c:153`)
and is **no longer touched by `startApex()`** — it used to force accel into
low-power mode and reset it to `ACCEL_CONFIG0_ODR_50_HZ`, clobbering
whatever `startAccel()` configured. `startApex()` now only sets the DMP's
own rate (`APEX_CONFIG1_DMP_ODR_50Hz`) and leaves accel running at 100Hz in
low-noise mode.

**Final accel ODR = 100Hz** (both accel and gyro, per `imu_start()`), DMP/APEX
internal rate = 50Hz. The only real constraint (datasheet + `inv_imu_apex.h`)
is `accel_ODR >= DMP_ODR`, which 100 ≥ 50 satisfies — APEX doesn't need to
run at the same rate as the FIFO output, and 50Hz is disproportionately good enough for pedometer/tilt/WOM without over-driving the DMP.

The FIFO captures whatever the accel/gyro produces, so FIFO data rate = accel
ODR = 100Hz.

### Trade-offs
- Accel now stays in **low-noise mode** the whole time (not the LP/duty-cycled
  mode `startApex()` used to force) — better data fidelity for logged FIFO
  samples, at the cost of the power savings LP mode would have given APEX.
  Revisit if a future power budget needs it back — the tradeoff was decided in
  favor of data quality for 100Hz NVS logging, not evaluated against battery life.
- If DMP_ODR ever needs to change, `APEX_CONFIG1_DMP_ODR_t` supports 25/50/100/400Hz
  (`inv_imu_defs.h`) — just keep it ≤ whatever `startAccel()`'s ODR is.

### Interrupt / NVS logging note (2026-07-30)
INT1 (P0.09, the only wired IMU interrupt pin on this board — INT2 doesn't
exist here, see "Interrupt Routing" below) carries the FIFO watermark only;
WOM is enabled in the `WOM_CONFIG` register by `startApex()` but is **not**
routed to a physical pin, so it can't wake the MCU via GPIO interrupt today.
At 100Hz the chip-side FIFO mirror (4KB, ~258 packets) only holds ~2.6s of
data, and `nvs_log_record()`/the IMU drain thread are fully paused for the
whole ~90s of a USB flash DUMP (`app_pause_background_threads()`) — so IMU
motion data logged to NVS is expected to have gaps during/around a dump.
Accepted tradeoff (Anders, 2026-07-30): dumps are periodic and ~2Gib of NVS
is available, so losing the in-flight FIFO window during an active dump is
fine — no buffering/backpressure work was done to avoid it.

---

## WOM (Wake on Motion)

Configured in `startApex()` in `ICM_42670.c`:

```c
inv_imu_configure_wom(&icm_driver, 180, 180, 180,
                      WOM_CONFIG_WOM_INT_MODE_ORED,
                      WOM_CONFIG_WOM_INT_DUR_3_SMPL);
```

### Parameters
- **Threshold (x/y/z)**: Units of 1/256g. Current value 180 ≈ 0.70g. Tune up to reduce false wakes, down if missing genuine raises.
- **WOM_INT_MODE `ORED`**: Fires if ANY axis exceeds threshold (correct for raise-to-wake)
- **WOM_INT_DUR `3_SMPL`**: Requires 3 consecutive samples above threshold before firing (~60ms at 50Hz). Filters single jolts/bumps.

### Use case: raise-to-wake
A deliberate wrist raise involves significant angular acceleration sustained over ~300–500ms, so a higher threshold + multi-sample duration reduces false positives from walking vibration or bumps.

---

## Initialization Chain & Callback

### Call chain (top to bottom)
```
main.c
  └── imu_init()                        [imu.c]
        └── init_icm()                  [ICM_42670.c]
              └── inv_imu_init()        [inv_imu_driver.c]
                    └── init_hardware_from_ui()   [inv_imu_driver.c]
                          └── inv_imu_configure_fifo()
```

Then back in `imu_init()`:
```
imu_start()                 → startAccel(), startGyro()
imu_fifo_interrupt()        → enableFifoInterrupt()
imu_apex()                  → startApex()
```

### Sensor event callback
`event_cb` is registered in `init_icm()` and passed into `inv_imu_init()`, which stores it on the `icm_driver` struct:

```c
// ICM_42670.c
static inv_imu_sensor_event_t* event;   // static pointer, not object-aware

void event_cb(inv_imu_sensor_event_t *evt) {
    memcpy(event, evt, sizeof(inv_imu_sensor_event_t)); // TODO: eliminate memcpy
    circular_buffer_add(imu_data_buffer, event);
}
```

The driver calls `s->sensor_event_cb(&event)` after parsing each FIFO packet or register read. The callback does a `memcpy` into a static buffer then pushes it onto the circular buffer (`imu_data_buffer`).

**Notes:**
- The `event` pointer is `static` because the callback has no `this`/context pointer — it can't capture object state
- The `memcpy` is acknowledged as a TODO — it's a copy from a stack-local event in the driver into the static buffer before handing off to the circular buffer
- The circular buffer is the handoff point between the driver callback and application-level processing

---

## Sensor Event Structure

Used to standardize any read from the device:

```c
typedef struct {
    int      sensor_mask;
    uint16_t timestamp_fsync;
    int16_t  accel[3];
#if ICM_IS_GYRO_SUPPORTED
    int16_t gyro[3];
#endif
    int16_t temperature;
    int8_t  accel_high_res[3];
#if ICM_IS_GYRO_SUPPORTED
    int8_t gyro_high_res[3];
#endif
} inv_imu_sensor_event_t;
```

### Timestamp
`timestamp_fsync` is a `uint16_t`, so max value is 0xFFFF (65,535). Need to think about what unit/resolution this represents and whether rollover needs to be handled when logging to flash.

---

## Interrupt Routing

- **INT1 (P0.9)**: WOM
- **INT2 (P0.3)**: FIFO threshold + FIFO full

### INT1 / P0.9 gotcha
P0.9 is the NFC1 antenna pin on nRF52832. Requires `nfct-pins-as-gpios` in the `&uicr` device tree node — `CONFIG_NFCT_PINS_AS_GPIOS=y` in prj.conf alone is **not sufficient**:

```dts
&uicr {
    gpio-as-nreset;
    nfct-pins-as-gpios;
};
```

Both INT1 and INT2 require push-pull output configuration on the IMU side.
