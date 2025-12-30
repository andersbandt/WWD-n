//*****************************************************************************
//!
//! @file st7735s
//! @brief ST7735S driver
//! @author Anders Bandt
//! @version 1.0
//! @date December 2025
//!
//*****************************************************************************

/* standard C file */
#include <stddef.h>

 /* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

/* My driver files */


#define DISP_NODE DT_NODELABEL(display0)

// static const struct gpio_dt_spec lcd_logic_power = GPIO_DT_SPEC_GET(DISP_NODE, lcd_logic_power_gpios);
// static const struct gpio_dt_spec lcd_backlight = GPIO_DT_SPEC_GET(DISP_NODE, lcd_backlight_power_gpios);
// static const struct gpio_dt_spec dc = GPIO_DT_SPEC_GET(DISP_NODE, cmd_data_gpios);
// static const struct gpio_dt_spec reset = GPIO_DT_SPEC_GET(DISP_NODE, reset_gpios);

// #define SPI_DEV DT_COMPAT_GET_ANY_STATUS_OKAY(sitronix_st7735s)
// #define SPI_OP SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE
// static struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(SPI_DEV, SPI_OP, 0);



