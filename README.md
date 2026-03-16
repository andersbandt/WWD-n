# WWD-n
WWD_prog using a Nordic Semiconductor (nRF52832)

## IMU Interrupt Routing

INT1 = WOM (Wake on Motion), INT2 = FIFO watermark/full.

### Call Stack

```
imu_init()                              [imu.c]
  └── init_icm()                        [ICM_42670.c]
        └── inv_imu_init()              [inv_imu_driver.c]
              └── init_hardware_from_ui()
                    ├── inv_imu_set_config_int1()
                    │     Writes: INT_SOURCE0, INT_SOURCE1, INT_SOURCE6_MREG1
                    │     WOM_X=ENABLE, WOM_Y=ENABLE, WOM_Z=ENABLE
                    │     FIFO_THS=DISABLE, FIFO_FULL=DISABLE, DRDY=DISABLE
                    │
                    └── inv_imu_set_config_int2()
                          Writes: INT_SOURCE3, INT_SOURCE4, INT_SOURCE7_MREG1
                          FIFO_THS=ENABLE, FIFO_FULL=ENABLE
                          WOM_X=DISABLE, WOM_Y=DISABLE, WOM_Z=DISABLE

imu_fifo_interrupt()    (IMU_FIFO_ENABLED=1)
  └── enableFifoInterrupt()             [ICM_42670.c]
        └── inv_imu_configure_fifo()
              Enables FIFO hardware — does NOT touch interrupt routing

imu_apex()              (IMU_APEX_ENABLED=0, currently disabled)
  └── startApex()                       [ICM_42670.c]
        ├── inv_imu_configure_wom()     Sets WOM thresholds/mode only
        └── inv_imu_enable_wom()        Sets WOM_EN bit — does NOT rewire routing
```

### Notes

- Interrupt routing is set once in `init_hardware_from_ui()` and is never overwritten by `enableFifoInterrupt()` or `startApex()`.
- WOM will not fire until `IMU_APEX_ENABLED` is set to `1` in `imu.h` (so `startApex()` is called and `inv_imu_enable_wom()` runs).
- Both INT1 and INT2 are configured as active-high push-pull in `init_hardware_from_ui()`.
