//*****************************************************************************
//!
//! @file button.h
//! @author Anders Bandt
//! @brief This header file is for controller user interface functions such as rotary encoder, buttons, displays, etc.
//! @version 1.0
//! @date January 2022
//!
//*****************************************************************************

#ifndef INTERFACECONTROL_H_
#define INTERFACECONTROL_H_

#include <stdint.h>
#include <stdbool.h>

/* Button bit masks for the button state byte. Physical layout (schematic
 * SW1-4, numbered clockwise from top-left) maps 1:1 to button1-4 in the DTS:
 *   SW1 = button1 = top-left     = UP
 *   SW2 = button2 = top-right    = SELECT
 *   SW3 = button3 = bottom-right = BACK
 *   SW4 = button4 = bottom-left  = DOWN
 * SW3+SW4 together (the bottom row) always returns to UI_MODE_CLOCK — see
 * handle_ui_input() in ui.c. */
#define BUTTON_1_MASK   0x01  /* SW1, top-left,     UP */
#define BUTTON_2_MASK   0x02  /* SW2, top-right,    SELECT; NEXT SCREEN in the time/date setter */
#define BUTTON_3_MASK   0x04  /* SW3, bottom-right, BACK; NEXT FIELD in the time/date setter */
#define BUTTON_4_MASK   0x08  /* SW4, bottom-left,  DOWN */

// External semaphore references (defined in interrupt.c)
extern struct k_sem button1_sem;
extern struct k_sem button2_sem;

/**
 * @brief Initialize button GPIO pins
 *
 * This function initializes the button GPIO pins as inputs.
 * Note: Interrupt configuration is handled separately in interrupt.c
 *
 * @return 0 on success, negative error code on failure
 */
int init_buttons(void);

/**
 * @brief Poll the current state of all buttons
 *
 * Reads the GPIO pins for all buttons and returns their state.
 * Active LOW: bit = 0 when button is pressed, bit = 1 when released
 *
 * @return uint8_t Button state byte (bits 0-3 for buttons 1-4)
 */
uint8_t button_poll(void);

/**
 * @brief Check if a specific button is currently pressed
 *
 * @param button_mask Button mask (e.g., BUTTON_1_MASK)
 * @return true if button is pressed (active low = 0)
 * @return false if button is released
 */
bool button_is_pressed(uint8_t button_mask);

/**
 * @brief Wait for a button press event using semaphores
 *
 * @param button_num Button number (1 or 2)
 * @param timeout_ms Timeout in milliseconds (K_FOREVER for infinite wait)
 * @return 0 if button was pressed, negative if timeout or error
 */
int button_wait_press(uint8_t button_num, int32_t timeout_ms);

/**
 * @brief Initialize the circular buffer for button inputs
 *
 * @return 0 on success, negative error code on failure
 */
int init_button_buffer(void);

/**
 * @brief Push a button event to the input buffer
 *
 * @param button_event Button state byte to add to buffer
 */
void button_buffer_push(uint8_t button_event);

/**
 * @brief Get the next button event from the input buffer
 *
 * @return uint8_t Button state byte, or 0 if buffer is empty
 */
uint8_t get_button_event(void);

/**
 * @brief Clear all button events from the input buffer
 *
 * Removes all pending button events from the circular buffer.
 */
void button_buffer_clear(void);

#endif /* INTERFACECONTROL_H_ */


