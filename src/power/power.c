//*****************************************************************************
//!
//! @file power.c
//! @author Anders Bandt
//! @brief Battery voltage sensing and power control
//! @version 1.0
//! @date 2026
//!
// Hardware:
//   VBAT_DIV_GPIO  = MCP23008 GP4 — this pin IS the 10k/10k divider's bottom leg
//                    directly (no FET, despite what earlier comments here said).
//                    Driven low (DTS ACTIVE_LOW, so gpio "asserted") grounds the
//                    bottom leg and completes the divider for a sample. Idle state
//                    must be high-impedance (input, not driven high or low) —
//                    driving it low continuously leaks VBAT->GND through both legs,
//                    and driving it high injects 3.3V into the divider node instead
//                    of grounding it, which was silently doubling every reading.
//   AIN2 (P0.04)   = divider output (VBAT / 2) when VBAT_DIV_GPIO is grounded
//   BOOST_SEL      = MCP23008 GP6  — TPS63900 mode select.
//                    MEASURED 2026-08-24 (VBAT sweep, DMM + 1.6R shunt, idle):
//                    HIGH (1) draws MORE power than LOW (0) at every supply
//                    voltage (+14.6% at 3.21 V falling to +2.8% at 4.37 V), so
//                    HIGH = the HIGHER 3.0 V rail and LOW = the lower 2.7 V rail
//                    — the opposite of what this comment claimed for a long
//                    time. Do not "correct" it back without re-measuring.
//
//                    CONSEQUENCE: power_save_enable(true) selects the MORE
//                    expensive rail; the name is backwards w.r.t. the hardware.
//                    It has no callers today — fix the sense before adding one.
//
//                    HAZARD: with BOOST_SEL held HIGH the board browns out at
//                    ~3.0 V and then will NOT restart until ~4.13 V (vs 3.21 V
//                    with it LOW) — the MCP23008 keeps GP6 driven after the MCU
//                    dies, so the converter stays in a mode that cannot start at
//                    low Vin. On a battery that is a dead-device trap: voltage
//                    only falls further, so recovery needs a charger.
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
        /* Idle state is high-impedance, not driven — see the file-header
         * comment. battery_voltage_mv() switches it to an output only for
         * the duration of a sample. */
        gpio_pin_configure_dt(&vbat_div_en, GPIO_INPUT);
    }

    power_rail_init();
}

/* Shadow of what was last written to BOOST_SEL. The MCP23008 pin is
 * write-only as far as this module is concerned (reading it back costs an I2C
 * transaction and would report the pin, not the converter's actual mode), so
 * the UI's low-power indicator reads this instead. Kept in sync by the only
 * two functions that drive the pin. */
static bool power_save_active;

/* Bring-up readout, mirrored into RAM for `nrfjprog --memrd` on a board with
 * no USB console attached. The MCP23008 ACKing an I2C scan only proves the
 * part answers; these say whether its Zephyr driver actually bound and
 * whether BOOST_SEL could be driven — which is what the TPS63900 mode select
 * (and so any power measurement) actually depends on. */
uint8_t boost_sel_ready;      /* gpio_is_ready_dt() result */
int8_t  boost_sel_cfg_ret;    /* gpio_pin_configure_dt() return */

void power_rail_init(void)
{
    boost_sel_ready = gpio_is_ready_dt(&boost_sel_gpio) ? 1 : 0;
    if (!boost_sel_ready) {
        LOG_ERR("BOOST_SEL GPIO not ready");
        return;
    }

    boost_sel_cfg_ret = (int8_t)gpio_pin_configure_dt(&boost_sel_gpio,
                                                     GPIO_OUTPUT_INACTIVE);
    power_save_active = false;
}

int battery_voltage_mv(void)
{
    int16_t raw;
    struct adc_sequence seq = {
        .buffer      = &raw,
        .buffer_size = sizeof(raw),
    };

    adc_sequence_init_dt(&adc_vbat, &seq);

    /* Ground the divider's bottom leg for the sample window only (see the
     * file-header comment — this pin is the leg itself, not a FET gate).
     * GPIO_OUTPUT_ACTIVE + the DTS ACTIVE_LOW flag together drive it low. */
    gpio_pin_configure_dt(&vbat_div_en, GPIO_OUTPUT_ACTIVE);
    k_sleep(K_MSEC(1));

    int err = adc_read_dt(&adc_vbat, &seq);

    /* Back to high-impedance immediately — leaving it driven (either
     * direction) either wastes current or corrupts the next sample. */
    gpio_pin_configure_dt(&vbat_div_en, GPIO_INPUT);

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
    power_save_active = enable;
}

bool power_save_is_enabled(void)
{
    return power_save_active;
}

void power_debug_hold_vbat_div(bool on)
{
    /* Profiling/debug only: battery_voltage_mv() pulses VBAT_DIV_EN for ~1ms
     * per real read, which isn't representative of "what does the divider's
     * own static draw look like held on continuously" — this bypasses the
     * pulse for that measurement. Not for normal runtime use.
     *
     * Must switch pin *mode* (input/output), not just the driven level —
     * see the file-header comment. Holding it as a driven output at 0 is
     * the correct "on" state (grounds the bottom leg); "off" has to release
     * back to input, not drive high. */
    gpio_pin_configure_dt(&vbat_div_en, on ? GPIO_OUTPUT_ACTIVE : GPIO_INPUT);
}

bool battery_charging(void)
{
    /* This board's BMS is a simple/discrete charge-management circuit, not an
     * I2C part — there is no register to read charging status from. Stubbed
     * false until/unless there's a charge-status GPIO worth wiring. */
    return false;
}
