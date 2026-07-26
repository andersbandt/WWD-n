//*****************************************************************************
//!
//! @file st7735s.c
//! @brief st7735 driver
//! @author Anders Bandt
//! @version 2.0
//! @date December 31st, 2025
//!
//*****************************************************************************

/* standard C file */
#include <stddef.h>

 /* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/pwm.h>


/* My driver files */
#include "st7735s_compat.h"


/* SPI definitions */
#define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(waveshare_st7735s)
#define SPI_OP SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE
static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);


/* GPIO definitions*/
#define DISP0_NODE DT_NODELABEL(st7735s)

static const struct gpio_dt_spec dc_dt = GPIO_DT_SPEC_GET(DISP0_NODE, dc_gpios);
static const struct gpio_dt_spec rs_dt = GPIO_DT_SPEC_GET(DISP0_NODE, reset_gpios);

/* Backlight: DISP_LED_PWM (P0.11) drives the gate of the LCD-power NMOS. */
static const struct pwm_dt_spec bl_pwm = PWM_DT_SPEC_GET(DISP0_NODE);


/* Backlight tracking */
uint8_t backlight_pct = 100;

/* SPI Initialization */
int SPI_Init_ST7735(void) {
    if (!spi_is_ready_dt(&spi_dev)) {
        return -1;
    }

    // initialize GPIO pins
    if (!gpio_is_ready_dt(&dc_dt)) return -2;
    if (!gpio_is_ready_dt(&rs_dt)) return -2;

    // set GPIO pins as inactive
    gpio_pin_configure_dt(&dc_dt, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&rs_dt, GPIO_OUTPUT_INACTIVE);

    if (!pwm_is_ready_dt(&bl_pwm)) return -3;

    return 0;
}


void Pin_RES_Active(void) {
    gpio_pin_set_dt(&rs_dt, 0);
}

void Pin_RES_Inactive(void) {
    gpio_pin_set_dt(&rs_dt, 1);
}

void Pin_DC_High(void) {
    gpio_pin_set_dt(&dc_dt, 1);
    k_busy_wait(10);
}

void Pin_DC_Low(void) {
    gpio_pin_set_dt(&dc_dt, 0);
    k_busy_wait(10);
}


void Pin_BLK_Pct(uint8_t pct) {
    backlight_pct = pct;

    pwm_set_pulse_dt(&bl_pwm, (bl_pwm.period * pct) / 100);
}

/* SPI Communication */
void SPI_send(uint16_t len, uint8_t *data) {
    struct spi_buf buf = {
        .buf = data,
        .len = len,
    };
    struct spi_buf_set set = {
        .buffers = &buf,
        .count = 1,
    };

    spi_write_dt(&spi_dev, &set);
}

void SPI_TransmitCmd(uint16_t len, uint8_t *data) {
    Pin_DC_Low();
    SPI_send(len, data);
}

void SPI_TransmitData(uint16_t len, uint8_t *data) {
    Pin_DC_High();
    SPI_send(len, data);
}

void SPI_Transmit(uint16_t len, uint8_t *data) {
    SPI_TransmitCmd(1, data);
    data++;
    if (--len) {
        SPI_TransmitData(len, data);
    }
}

