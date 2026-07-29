//*****************************************************************************
//!
//! @file power.c
//! @author Anders Bandt
//! @brief Battery voltage sensing and power control
//! @version 1.0
//! @date 2026
//!
// Hardware:
//   VBAT_DIV_GPIO  = MCP23008 GP4  — enables 10k/10k voltage divider FET (active HIGH)
//   AIN2 (P0.04)   = divider output (VBAT / 2)
//   BOOST_SEL      = MCP23008 GP6  — TPS63900 mode select
//                    (active HIGH = LOWER VCC output voltage, i.e. power-save mode;
//                    LOW = normal/higher output. Previously documented backwards.)
//   BMS            = simple/discrete charge-management circuit, no I2C — this board
//                    does not have a BQ25120A
//*****************************************************************************

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include <power/power.h>

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

/* ADC channel from DTS zephyr,user io-channels[0] */
static const struct adc_dt_spec adc_vbat = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

/* Power control GPIOs from DTS power_ctrl node */
static const struct gpio_dt_spec vbat_div_en =
    GPIO_DT_SPEC_GET(DT_NODELABEL(vbat_div_en), gpios);
static const struct gpio_dt_spec boost_sel_gpio =
    GPIO_DT_SPEC_GET(DT_NODELABEL(boost_sel), gpios);

void power_init(void)
{
    int err = 0;

    if (!adc_is_ready_dt(&adc_vbat)) {
        LOG_ERR("ADC not ready");
        err = -ENODEV;
    } else {
        err = adc_channel_setup_dt(&adc_vbat);
        if (err) {
            LOG_ERR("ADC channel setup failed: %d", err);
        }
    }

    if (!gpio_is_ready_dt(&vbat_div_en)) {
        LOG_ERR("VBAT_DIV_EN GPIO not ready");
    } else {
        gpio_pin_configure_dt(&vbat_div_en, GPIO_OUTPUT_INACTIVE);
    }

    if (!gpio_is_ready_dt(&boost_sel_gpio)) {
        LOG_ERR("BOOST_SEL GPIO not ready");
    } else {
        gpio_pin_configure_dt(&boost_sel_gpio, GPIO_OUTPUT_INACTIVE);
    }
}

int battery_voltage_mv(void)
{
    int16_t raw;
    struct adc_sequence seq = {
        .buffer      = &raw,
        .buffer_size = sizeof(raw),
    };

    adc_sequence_init_dt(&adc_vbat, &seq);

    /* Enable the divider FET, allow a brief settling time */
    gpio_pin_set_dt(&vbat_div_en, 1);
    k_sleep(K_MSEC(1));

    int err = adc_read_dt(&adc_vbat, &seq);

    gpio_pin_set_dt(&vbat_div_en, 0);

    if (err) {
        LOG_ERR("adc_read failed: %d", err);
        return 0;
    }

    /* Convert raw sample to mV at the ADC pin, then ×2 for the 1:2 divider */
    int32_t val_mv = raw;
    adc_raw_to_millivolts_dt(&adc_vbat, &val_mv);
    return (int)(val_mv * 2);
}

uint8_t battery_percent(int mv)
{
    /* Approximate LiPo discharge curve: 4200mV = 100%, 3000mV = 0% */
    if (mv >= 4200) return 100;
    if (mv <= 3000) return 0;
    return (uint8_t)((mv - 3000) * 100 / (4200 - 3000));
}

void power_save_enable(bool enable)
{
    gpio_pin_set_dt(&boost_sel_gpio, enable ? 1 : 0);
}

bool battery_charging(void)
{
    /* This board's BMS is a simple/discrete charge-management circuit, not an
     * I2C part — there is no register to read charging status from. Stubbed
     * false until/unless there's a charge-status GPIO worth wiring. */
    return false;
}
