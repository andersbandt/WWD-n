//*****************************************************************************
//! @file ble.c
//! @brief BLE peripheral — live status notifications and clock sync.
//!        See ble.h for why the scope is deliberately this small.
//*****************************************************************************

#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_BT
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>
#endif

#include <ble/ble.h>
#include <peripheral/clock.h>
#include <peripheral/rv3028.h>

LOG_MODULE_REGISTER(ble, CONFIG_LOG_DEFAULT_LEVEL);

#ifdef CONFIG_BT

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* How often to push a status notification while someone is listening. 2 s
 * rather than the 1 s UI tick: nothing here changes fast (temperatures move
 * slowly, steps are a running total), and each notification is a radio event
 * that costs power on a device whose whole budget is ~2.6 mA. */
#define STATUS_NOTIFY_INTERVAL  K_SECONDS(2)

/* ---- UUIDs ---------------------------------------------------------------
 * Custom 128-bit base, because none of this maps onto an adopted service.
 * Battery level deliberately is NOT exposed as the standard Battery Service:
 * BAS reports integer percent, and percent is derived from a LiPo curve this
 * project has not validated (battery_percent() in power.c is a placeholder
 * linear map). Reporting raw millivolts is honest; a phone can do what it
 * likes with it.
 */
#define WWD_UUID_BASE(x) \
    BT_UUID_128_ENCODE(0xa7f30000u | (x), 0x6b5d, 0x4f2e, 0x9c88, 0x2d1f7e4a0b11)

static const struct bt_uuid_128 wwd_svc_uuid    = BT_UUID_INIT_128(WWD_UUID_BASE(1));
static const struct bt_uuid_128 wwd_status_uuid = BT_UUID_INIT_128(WWD_UUID_BASE(2));
static const struct bt_uuid_128 wwd_time_uuid   = BT_UUID_INIT_128(WWD_UUID_BASE(3));

/* ---- Status payload ------------------------------------------------------
 * 19 bytes, which fits the default 23-byte ATT MTU (3 bytes of which are
 * header) in a single packet with no negotiation. Keeping it inside the
 * default MTU means it works against any central without an MTU exchange.
 *
 * Little-endian and __packed so the layout is the wire format — a host-side
 * decoder can struct.unpack("<IhhhIBHBB") it directly.
 */
struct __packed wwd_status {
    uint32_t uptime_s;
    int16_t  batt_mv;
    int16_t  imu_temp_raw;      /* raw ICM-42670 register value, not degrees */
    int16_t  soc_temp_centi_c;  /* nRF die temp, hundredths of a degree C */
    uint32_t steps;
    uint8_t  activity_id;       /* activity_id_t; 0 = none running */
    uint16_t session_seq;       /* the run index, valid only if activity_id != 0 */
    uint8_t  worn;              /* wear detection state */
    uint8_t  time_valid;        /* RTC has been set */
};

static struct wwd_status status_cache;
static struct k_mutex    status_lock;
static bool              ready;
static struct bt_conn   *current_conn;
static bool              status_subscribed;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* Service UUID goes in the scan response, not the advertisement: a 128-bit
 * UUID is 16 bytes and the name already takes most of the 31-byte adv packet.
 * Splitting them means a scanner sees the name immediately and the UUID on
 * request, instead of the name being truncated. */
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, WWD_UUID_BASE(1)),
};


static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);

    struct wwd_status snapshot;

    k_mutex_lock(&status_lock, K_FOREVER);
    snapshot = status_cache;
    k_mutex_unlock(&status_lock);

    /* uptime is the one field generated here rather than cached — it is free
     * and it lets a reader tell a live device from a stale cache. */
    snapshot.uptime_s = (uint32_t)(k_uptime_get() / 1000);

    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             &snapshot, sizeof(snapshot));
}


/*
 * Clock sync: write 4 bytes, little-endian UNIX seconds (UTC).
 *
 * Writes the BCD calendar block, NOT the RV-3028's separate 32-bit UNIX
 * counter — rv3028_set_unix_time() touches a different register set that is
 * not synced with the calendar registers get_current_time()/get_date() read,
 * so setting it would leave the displayed clock and TIME_ANCHOR unchanged.
 * That distinction is documented in rv3028.h and is easy to get wrong.
 */
static ssize_t write_time(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          const void *buf, uint16_t len, uint16_t offset,
                          uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len != sizeof(uint32_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint32_t unix_s = sys_get_le32(buf);

    /* Sanity-floor the value. A zero or obviously-bogus timestamp would set
     * the clock to 1970 and, worse, make rv3028_time_is_set() report false
     * afterwards — silently reverting every future dump to relative time. */
    if (unix_s < 1735689600u) {   /* 2025-01-01T00:00:00Z */
        LOG_WRN("BLE time-set rejected: %u is before 2025", unix_s);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    time_t     t = (time_t)unix_s;
    struct tm  tm_buf;

    if (gmtime_r(&t, &tm_buf) == NULL) {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    Date d = {
        .day   = (uint8_t)tm_buf.tm_mday,
        .month = (uint8_t)(tm_buf.tm_mon + 1),
        .year  = (uint16_t)(tm_buf.tm_year + 1900),
    };
    Time tm_out = {
        .hours   = (int8_t)tm_buf.tm_hour,
        .minutes = (int8_t)tm_buf.tm_min,
        .seconds = (int8_t)tm_buf.tm_sec,
    };

    /* Date first, then time: rv3028_set_time() reads the existing date back
     * and rewrites it alongside the new time (they share one register block),
     * so setting the date second would be overwritten by a stale read. */
    rv3028_set_date(d);
    rv3028_set_time(tm_out);

    LOG_INF("BLE time-set: %04u-%02u-%02u %02u:%02u:%02u UTC",
            d.year, d.month, d.day, tm_out.hours, tm_out.minutes, tm_out.seconds);

    return (ssize_t)len;
}


static void status_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    status_subscribed = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("BLE status notifications %s", status_subscribed ? "on" : "off");
}


/* Attribute 1 is the status value, 3 is the time-set value — indices matter
 * for the bt_gatt_notify() below and shift if attributes are reordered. */
BT_GATT_SERVICE_DEFINE(wwd_svc,
    BT_GATT_PRIMARY_SERVICE(&wwd_svc_uuid),

    BT_GATT_CHARACTERISTIC(&wwd_status_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_status, NULL, NULL),
    BT_GATT_CCC(status_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    BT_GATT_CHARACTERISTIC(&wwd_time_uuid.uuid,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, write_time, NULL),
);


static void notify_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(notify_work, notify_work_fn);

static void notify_work_fn(struct k_work *work)
{
    ARG_UNUSED(work);

    if (current_conn == NULL || !status_subscribed) {
        return;   /* rescheduled on the next connect/subscribe */
    }

    struct wwd_status snapshot;

    k_mutex_lock(&status_lock, K_FOREVER);
    snapshot = status_cache;
    k_mutex_unlock(&status_lock);

    snapshot.uptime_s = (uint32_t)(k_uptime_get() / 1000);

    /* &wwd_svc.attrs[1] is the status characteristic's VALUE attribute. */
    int err = bt_gatt_notify(current_conn, &wwd_svc.attrs[1],
                             &snapshot, sizeof(snapshot));
    if (err) {
        LOG_WRN("bt_gatt_notify failed: %d", err);
    }

    k_work_schedule(&notify_work, STATUS_NOTIFY_INTERVAL);
}


static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("BLE connection failed: %u", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    LOG_INF("BLE connected");
    k_work_schedule(&notify_work, STATUS_NOTIFY_INTERVAL);
}


static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE disconnected (reason %u)", reason);

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    status_subscribed = false;
    (void)k_work_cancel_delayable(&notify_work);

    /* Zephyr does not restart advertising itself after a disconnect. Without
     * this the device is invisible until reboot. */
    int rc = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (rc && rc != -EALREADY) {
        LOG_ERR("re-advertise failed: %d", rc);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected,
    .disconnected = disconnected,
};

#endif /* CONFIG_BT */


void ble_init(void)
{
#ifdef CONFIG_BT
    k_mutex_init(&status_lock);

    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed: %d", err);
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("bt_le_adv_start failed: %d", err);
        return;
    }

    ready = true;
    LOG_INF("BLE advertising as \"%s\"", DEVICE_NAME);
#else
    LOG_WRN("ble_init called but CONFIG_BT not enabled");
#endif
}


bool ble_is_ready(void)
{
#ifdef CONFIG_BT
    return ready;
#else
    return false;
#endif
}


bool ble_is_connected(void)
{
#ifdef CONFIG_BT
    return current_conn != NULL;
#else
    return false;
#endif
}


void ble_publish_status(uint16_t batt_mv,
                        int16_t  imu_temp_raw,
                        int16_t  soc_temp_centi_c,
                        uint32_t steps,
                        uint8_t  activity_id,
                        uint16_t session_seq,
                        bool     worn,
                        bool     time_valid)
{
#ifdef CONFIG_BT
    k_mutex_lock(&status_lock, K_FOREVER);
    status_cache.batt_mv          = (int16_t)batt_mv;
    status_cache.imu_temp_raw     = imu_temp_raw;
    status_cache.soc_temp_centi_c = soc_temp_centi_c;
    status_cache.steps            = steps;
    status_cache.activity_id      = activity_id;
    status_cache.session_seq      = session_seq;
    status_cache.worn             = worn ? 1 : 0;
    status_cache.time_valid       = time_valid ? 1 : 0;
    k_mutex_unlock(&status_lock);
#else
    ARG_UNUSED(batt_mv); ARG_UNUSED(imu_temp_raw); ARG_UNUSED(soc_temp_centi_c);
    ARG_UNUSED(steps); ARG_UNUSED(activity_id); ARG_UNUSED(session_seq);
    ARG_UNUSED(worn); ARG_UNUSED(time_valid);
#endif
}
