//*****************************************************************************
//! @file soc_temp.c
//! @brief nRF52833 on-die temperature sensor. See soc_temp.h for why.
//*****************************************************************************

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <peripheral/soc_temp.h>

LOG_MODULE_REGISTER(soc_temp, CONFIG_LOG_DEFAULT_LEVEL);

/* DEVICE_DT_GET_ONE rather than DT_NODELABEL(temp): the SoC has exactly one
 * TEMP instance, and matching on the compatible means this keeps working if
 * the node label ever changes upstream. Resolved at compile time, so a missing
 * node is a build error rather than a silent NULL. */
static const struct device *temp_dev = DEVICE_DT_GET_ONE(nordic_nrf_temp);

static bool ready;


int soc_temp_init(void)
{
    if (!device_is_ready(temp_dev)) {
        /* Almost always means CONFIG_TEMP_NRF5=n — the node is enabled in the
         * SoC dtsi regardless, so the device exists but never gets a driver. */
        LOG_ERR("SoC TEMP device not ready (is CONFIG_TEMP_NRF5=y?)");
        ready = false;
        return -ENODEV;
    }

    ready = true;

    int16_t centi;
    int rc = soc_temp_read_centi_c(&centi);

    if (rc != 0) {
        LOG_ERR("SoC TEMP bound but first read failed: %d", rc);
        /* Leave `ready` set: the device bound fine, so this is more likely a
         * transient fetch failure than a missing driver, and later reads are
         * worth attempting. Callers see the error per-read either way. */
        return rc;
    }

    LOG_INF("SoC die temp: %d.%02d C", centi / 100, abs(centi % 100));
    return 0;
}


bool soc_temp_available(void)
{
    return ready;
}


int soc_temp_read_centi_c(int16_t *out_centi_c)
{
    if (!ready || out_centi_c == NULL) {
        return -ENODEV;
    }

    int rc = sensor_sample_fetch(temp_dev);

    if (rc != 0) {
        return rc;
    }

    struct sensor_value val;

    rc = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &val);
    if (rc != 0) {
        return rc;
    }

    /* sensor_value is val1 whole units + val2 millionths, both signed and both
     * carrying the sign for negative readings (e.g. -5.25 C is val1=-5,
     * val2=-250000), so this arithmetic works below zero without special
     * casing. Truncation toward zero costs at most 0.01 C, well under the
     * sensor's 0.25 C resolution. */
    int32_t centi = (val.val1 * 100) + (val.val2 / 10000);

    /* The part is specified -40..+85 C, so -4000..8500 always fits an int16.
     * Clamp anyway rather than letting a garbage reading wrap silently — a
     * pinned value is obvious in a dump, a wrapped one looks like real data. */
    if (centi > INT16_MAX) {
        centi = INT16_MAX;
    } else if (centi < INT16_MIN) {
        centi = INT16_MIN;
    }

    *out_centi_c = (int16_t)centi;
    return 0;
}


float soc_temp_centi_c_to_f(int16_t centi_c)
{
    return ((float)centi_c / 100.0f) * 1.8f + 32.0f;
}
