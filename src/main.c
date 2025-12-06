//*****************************************************************************
//!
//! @file led.h
//! @author Anders Bandt
//! @brief Main code for WWD with Nordic
//! @version 0.9
//! @date December 2025
//!
//*****************************************************************************

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>


// my driver files
#include <hardware/led.h>
#include <memory/mt29f_nand.h>



int main(void)
{
        // run initialization functions
        led_init();


        // main loop
        while (1) {
                led_fast_blink(30);
        }
	return 0;
}

