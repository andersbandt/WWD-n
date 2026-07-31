#ifndef SRC_MAIN_H_
#define SRC_MAIN_H_

/* Parks every background application thread (sensor_update, ui_refresh,
 * button_handler — UI updates, IMU FIFO drain, temp/anchor logging) at their
 * own wake boundary for the duration of a flash maintenance operation
 * (DUMP/ERASE), so the CPU only runs the protocol thread's own work and none
 * of them contend for the SPI1 bus shared with the NAND driver. See the
 * bg_pause_active/bg_park_if_paused() comments in main.c for why this is a
 * cooperative handshake rather than k_thread_suspend(). */
void app_pause_background_threads(void);
void app_resume_background_threads(void);

#endif /* SRC_MAIN_H_ */
