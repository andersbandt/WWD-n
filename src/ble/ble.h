#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * BLE peripheral: live status out, clock sync in.
 *
 * Scope is deliberate. This is NOT a bulk transport — a full log dump is
 * ~285 MB, and even at a good BLE rate that is hours, so log extraction stays
 * on USB where it already runs at ~84 KB/s. What BLE is good at is small
 * payloads and convenience, so that is all it does: a periodic status
 * notification, and a way to set the clock.
 *
 * The clock half is the part that earns its keep immediately. TIME_ANCHOR now
 * records real wall-clock time, but the device has no way to LEARN the correct
 * time — it has no keypad worth the name and USB carries no time command. A
 * phone that knows the time can now set it in one write.
 */

/** @brief Bring up the BLE stack and start advertising. Safe to call once. */
void ble_init(void);

/** @brief True once bt_enable() has succeeded. */
bool ble_is_ready(void);

/** @brief True while a central is connected. */
bool ble_is_connected(void);

/**
 * @brief Hand BLE a fresh snapshot of device state to publish.
 *
 * Called from sensor_update_thread, which already reads every one of these
 * for the clock face. That is the whole point: the BLE notification path must
 * NOT sample anything itself. Every value here comes from SPI1 or I2C, and
 * sampling them from the Bluetooth or system workqueue would put a new thread
 * on a bus that the dump-pause handshake (app_pause_background_threads() in
 * main.c) does not know about, and that the measured stack budget does not
 * account for. This function only copies into a cache; the notify work sends
 * whatever the cache last held.
 */
void ble_publish_status(uint16_t batt_mv,
                        int16_t  imu_temp_raw,
                        int16_t  soc_temp_centi_c,
                        uint32_t steps,
                        uint8_t  activity_id,
                        uint16_t session_seq,
                        bool     worn,
                        bool     time_valid);

#endif /* BLE_H */
