# IMU Notes (ICM-42670P)

## ODR (Output Data Rate)

The accel ODR is set in two places during init, and the order matters:

1. `imu_start()` → `startAccel(100, ...)` → sets accel ODR to 100Hz
2. `imu_apex()` → `startApex()` → **overwrites** accel ODR to 50Hz, DMP also set to 50Hz

**Final accel ODR = 50Hz**, regardless of what `startAccel` sets.

The FIFO captures whatever the accel/gyro produces, so FIFO data rate = accel ODR.

### Trade-offs
- APEX (pedometer, tilt, WOM) works well at 50Hz and benefits from lower ODR for power savings
- For high-rate data streaming to NAND flash, higher ODR (100–200Hz) is desirable
- Currently left at 50Hz. To change, modify `ACCEL_CONFIG0_ODR_50_HZ` in `startApex()` in `ICM_42670.c`

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
