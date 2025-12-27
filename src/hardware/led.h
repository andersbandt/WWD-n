//*****************************************************************************
//!
//! @file led.h
//! @author Anders Bandt
//! @brief Functions to drive LED
//! @version 0.9
//! @date September 2023
//!
//*****************************************************************************


#ifndef SRC_HARDWARE_LED_H_
#define SRC_HARDWARE_LED_H_



int led_init();


void led_set(int led, int state);

void led_blink(int led);



/**
 * @brief blinks the led even more rapidly
 *
 * @param mult is how long to sleep between toggles. Longer means longer blink
 *
 */
void led_fast_blink(int mult);




/**
 * @brief blinks the led even more rapidly
 *
 * @param mult      how long to sleep between toggles. Longer means longer blink
 *
 */
void led_config_blink(int mult, int num_blinks);



/**
 * @brief display a digit through singular LED as interface
 *
 * @param num       digit to print
 *
 */
void led_disp_num(int num);






#endif /* SRC_HARDWARE_LED_H_ */
