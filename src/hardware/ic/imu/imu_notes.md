# IMU Notes

## High-Resolution Mode (`IMU_HIGH_RES_ENABLED`)

The `inv_imu_sensor_event_t` struct has `accel_high_res[3]` and `gyro_high_res[3]` fields
for 20-bit accel/gyro data. These are gated behind `IMU_HIGH_RES_ENABLED` in `imu.h`.

**Toggling the define only changes the struct layout.** To actually receive high-res data
you also need to configure the FIFO packet format on the hardware side (20-bit packet mode)
— that register configuration is not yet implemented.

## Event Buffer Heap Constraint

The IMU event buffer is allocated in `imu_init()`:
```c
circular_buffer_init(200, sizeof(inv_imu_sensor_event_t))
```

Total heap consumed = `200 * sizeof(inv_imu_sensor_event_t)`.

`CONFIG_HEAP_MEM_POOL_SIZE=4096` (4 KB) in `prj.conf` — this is tight given the current
struct size. Two things will push you over the limit:

- **Enabling `IMU_HIGH_RES_ENABLED`** adds 3–6 bytes per slot (×200 = 600–1200 bytes extra)
- **Increasing slot count** past 200

If `circular_buffer_init` returns NULL (malloc fails silently), `circular_buffer_add` will
crash. Worth adding a NULL check on `imu_data_buffer` after init.

Mitigations:
- Reduce slot count — 32–64 is likely sufficient for a FIFO drained on every interrupt
- Increase `CONFIG_HEAP_MEM_POOL_SIZE` in `prj.conf`
