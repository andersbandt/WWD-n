#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_BT
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#endif

#include <ble/ble.h>

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

#ifdef CONFIG_BT

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("BLE connection failed: %u", err);
        return;
    }
    LOG_INF("BLE connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("BLE disconnected (reason %u)", reason);
    bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
};

#endif /* CONFIG_BT */

void ble_init(void)
{
#ifdef CONFIG_BT
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed: %d", err);
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("bt_le_adv_start failed: %d", err);
        return;
    }

    LOG_INF("BLE advertising started as \"%s\"", DEVICE_NAME);
#else
    LOG_WRN("ble_init called but CONFIG_BT not enabled");
#endif
}
