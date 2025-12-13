//*****************************************************************************
//!
//! @file led.h
//! @author Anders Bandt
//! @brief Main code for WWD with Nordic
//! @version 0.9
//! @date December 2025
//!
//*****************************************************************************


// standard C file
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

// Zephyr files
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

// my driver files
#include <hardware/led.h>
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
    LOG_INF("Starting WWD program!");

	flash_init();
    uint8_t data = 8;
	//log_append(&data, 1);

    // main loop
    while (1) {
        flash_init();
        led_fast_blink(30);
        LOG_INF("heartbeat ...");
        k_msleep(3000);
    }
	return 0;
}

