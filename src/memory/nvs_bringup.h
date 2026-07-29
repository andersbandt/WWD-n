#ifndef MEMORY_NVS_BRINGUP_H
#define MEMORY_NVS_BRINGUP_H

/* NVS / MT29F bring-up phase — nvs_init(), the current NVS_BRINGUP_STEP test
 * (baseline/write/recover/pipeline/plane-test), and the SPI1 coexistence
 * check against the IMU. Call once from main() after imu_probe(). */
void nvs_bringup_phase(void);

/* Called once per heartbeat second (from ui_refresh_thread) to drive the
 * NVS_STEP_PIPELINE logging cadence: IMU FIFO drain, periodic temperature
 * record, periodic time anchor. No-op for any other NVS_BRINGUP_STEP. */
void nvs_pipeline_tick(void);

#endif /* MEMORY_NVS_BRINGUP_H */
