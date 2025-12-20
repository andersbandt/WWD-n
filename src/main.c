//*****************************************************************************
//!
//! @file led.h
//! @author Anders Bandt
//! @brief Main code for WWD with Nordic
//! @version 0.9
//! @date December 2025
//!
//*****************************************************************************


/* standard C file */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* My driver files */
#include <hardware/led.h>
#include <peripheral/interrupt.h>
// #include <imu.h>
#include <icm42670.h>
#include <memory/mt29f_nand.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


static const mt29f_cfg_t cfg = {
    .num_dies = 2,
    .blocks_per_die = 1024,
    .pages_per_block = 64,
    .bytes_per_page = 2176,
    .oob_bytes = 128
};

static off_t write_addr = 0;
static off_t read_addr = 0;


void flash_init(void)
{
    mt29f_init(&cfg);
}

void log_append(const uint8_t *data, size_t len)
{
    if (len % cfg.bytes_per_page == 0) {
        LOG_ERR("Write in blocks of page bytes!");
        return;
    }

    mt29f_write(write_addr, data, len);
    write_addr += len;
}

size_t log_read(uint8_t *data, size_t len)
{
    if (len % cfg.bytes_per_page == 0) {
        LOG_ERR("Read in blocks of page bytes!");
        return 0;
    }

    if (read_addr == write_addr) {
        LOG_INF("No more data to read!");
        return 0;
    }

    mt29f_read(read_addr, data, len);
    read_addr += len;

    return len;
}



int main(void)
{
    // run initialization functions
    led_init();
	led_fast_blink(10);

    // initialize interrupts
    config_all_interrupts();
    LOG_INF("Starting WWD program!");




    // LCD INIT
    // ----------------------------------------------------------
    ST7789_Init (&lcd, ST77XX_ROTATE_270 | ST77XX_RGB);

    // DRAWING
    // ----------------------------------------------------------
    ST7789_ClearScreen (&lcd, WHITE);
    for (i=0; i<Screen.height; i=i+5) {
        ST7789_DrawLine (&lcd, 0, Screen.width, 0, i, RED);
    }
    for (i=0; i<Screen.height; i=i+5) {
        ST7789_DrawLine (&lcd, 0, Screen.width, i, 0, BLUE);
    }
    for (i=0; i<30; i++) {
        ST7789_FastLineHorizontal (&lcd, 0, Screen.width, i, BLACK);
    }
    ST7789_SetPosition (75, 5);
    ST7789_DrawString (&lcd, "ST7789V2 DRIVER", WHITE, X3);


    struct icm42670_data sensor_data;

    printk("Starting ICM42670 application\n");

    /* Initialize the sensor */
    int ret = 0;
    ret |= icm42670_init();
    // ret |= imu_init();


    if (ret == 0) {
        printk("Initialized IMU\n");
    }
    else {
        printk("Failed to initialize ICM42670\n");
        // return;
    }


	//flash_init();
    //uint8_t data = 8;
	//log_append(&data, 1);

    // main loop
    while (1) {
        LOG_INF("heartbeat ...");
        // led_fast_blink(30);
        // k_msleep(3000);


        if (BUTTON_1_INT_FLAG) {
            led_blink(2);
            BUTTON_1_INT_FLAG = 0;
        }
        if (BUTTON_2_INT_FLAG) {
            led_blink(3);
            BUTTON_2_INT_FLAG = 0;
        }
    }
	return 0;
}

