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


/* My driver files */
#include "st7735s_compat.h"


/* SPI definitions */
#define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(waveshare_st7735s)
#define SPI_OP SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE
static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);


/* GPIO definitions*/
#define DISP0_NODE DT_NODELABEL(st7735s) // TODO: for some reason `display0` doesn't work here like it does in my ST7789 driver

static const struct gpio_dt_spec dc_dt = GPIO_DT_SPEC_GET(DISP0_NODE, dc_gpios);
static const struct gpio_dt_spec rs_dt = GPIO_DT_SPEC_GET(DISP0_NODE, reset_gpios);


/* Backlight tracking */
uint8_t backlight_pct = 100;

/* SPI Initialization */
void SPI_Init_ST7735(void) {
    if (!spi_is_ready_dt(&spi_dev)) {
        /* TODO: Better error handling */
        while (1) { }
    }

    /* Initialize GPIO pins */
    if (!gpio_is_ready_dt(&dc_dt)) while (1) { }
    if (!gpio_is_ready_dt(&rs_dt)) while (1) { }

    gpio_pin_configure_dt(&dc_dt, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&rs_dt, GPIO_OUTPUT_INACTIVE);
}


// TODO: refactor these reset functions to be more intuitive (with it being ACTIVE_LOW and all)
void Pin_RES_High(void) {
    gpio_pin_set_dt(&rs_dt, 0);
}

void Pin_RES_Low(void) {
    gpio_pin_set_dt(&rs_dt, 1);
}

void Pin_DC_High(void) {
    gpio_pin_set_dt(&dc_dt, 1);
    // k_msleep(1);
}

void Pin_DC_Low(void) {
    gpio_pin_set_dt(&dc_dt, 0);
    // k_msleep(1);
}


void Pin_BLK_Pct(uint8_t pct) {
    backlight_pct = pct;

    /* Simple on/off control (no PWM for now) */
    if (pct > 0) {
        // gpio_pin_set_dt(&bl_dt, 1);
    } else {
        // gpio_pin_set_dt(&bl_dt, 0);
    }
}

/* SPI Communication */
// TODO (small): can I combine this one with the other display transport layers?
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

