//*****************************************************************************
//!
//! @file led.c
//! @author Anders Bandt
//! @brief Functions to drive LED
//! @version 0.9
//! @date September 2023
//!
//*****************************************************************************


/* standard C99 stuff */
#include <unistd.h>

/* Zephyr files  */
#include <zephyr/drivers/gpio.h>


/* My header files */
#include <hardware/led.h>


/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)


/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);



int led_init()
{
    if (!gpio_is_ready_dt(&led0)) {
		return 0;
	}
    if (!gpio_is_ready_dt(&led1)) {
		return 0;
	}
    if (!gpio_is_ready_dt(&led2)) {
		return 0;
	}

	int ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
    ret |= gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
    ret |= gpio_pin_configure_dt(&led2, GPIO_OUTPUT_ACTIVE);

	if (ret < 0) {
		return 0;
	}
    else {
        return 1;
    }
}



const struct gpio_dt_spec *pick_led(int num)
{
    switch (num) {
    case 1: return &led0;
    case 2: return &led1;
    case 3: return &led2;
    default: return NULL;
    }
}


/*
 * blinks the led rapidly and also leaves it in an OFF state
 */
void led_blink(int led)
{
    const struct gpio_dt_spec * led_dt = pick_led(led);

    for (int i=0; i<12; i++) {
        // toggle LED
        gpio_pin_toggle_dt(led_dt);
        k_usleep(100000);
    }
    gpio_pin_set_dt(led_dt, 0);
}


/*
 * blinks the led even more rapidly and also leaves it in an OFF state
 */
void led_fast_blink(int mult)
{
    for(int i=0; i<6; i++) {
        // toggle LED
        gpio_pin_toggle_dt(&led0);
        k_usleep(mult * 3000);
    }
    gpio_pin_set_dt(&led0, 0);
}



void led_config_blink(int mult, int num_blinks) {
    gpio_pin_set_dt(&led0, 0);
    for(int i=0; i<num_blinks; i++) {
        // toggle LED
        gpio_pin_toggle_dt(&led0);
        k_usleep(mult);
        gpio_pin_toggle_dt(&led0);
        k_usleep(mult);
    }
    gpio_pin_set_dt(&led0, 0);
}



/*
 * led_disp_num: display a sequence as a series of digit to digit LED blinks
 *     NOTE that this function display the digit in order from ones place to the highest place
 */
void led_disp_num(int num) {
    int rem;
    
    // display start of digit signifier
    led_config_blink(30*1000, 12);
    sleep(4);
    
    // loop through digits
    while (num > 0) {
        rem = num % 10;
        led_config_blink(700*1000, rem);
        sleep(2);
        // display between digit signifier
        led_config_blink(30*1000, 3);
        sleep(1);
        // chop off last digit of int
        num = num / 10;        
    }

    sleep(1);
    // display end of digit signifier
    led_config_blink(30*1000, 12);
}


