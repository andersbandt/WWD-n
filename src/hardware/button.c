//*****************************************************************************
//!
//! @file button.c
//! @author Anders Bandt
//! @brief Function descriptions for driving buttons
//! @version 1.0
//! @date August 11th, 2024
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Zephyr files  */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>

/* My header files */
#include <hardware/button.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL VARIABLES -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Get button GPIO specs from devicetree (same as interrupt.c)
static const struct gpio_dt_spec btn1 = GPIO_DT_SPEC_GET(DT_NODELABEL(button1), gpios);
static const struct gpio_dt_spec btn2 = GPIO_DT_SPEC_GET(DT_NODELABEL(button2), gpios);
static const struct gpio_dt_spec btn3 = GPIO_DT_SPEC_GET(DT_NODELABEL(button3), gpios);
static const struct gpio_dt_spec btn4 = GPIO_DT_SPEC_GET(DT_NODELABEL(button4), gpios);

// Holds the current state of each button. A 0 in a bit indicates
// that button is currently pressed (active low), otherwise it is released.
static uint8_t g_ui8ButtonStates = 0xFF;  // Start with all buttons released

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes the GPIO pins used by the board pushbuttons
 *
 * This function must be called during application initialization to
 * configure the GPIO pins to which the pushbuttons are attached.
 * Note: This only configures the pins as inputs. Interrupt configuration
 * is handled in interrupt.c via config_all_interrupts().
 *
 * @return 0 on success, negative error code on failure
 */
int init_buttons(void)
{
    int ret = 0;

    // Check if all button GPIOs are ready
    if (!gpio_is_ready_dt(&btn1) || !gpio_is_ready_dt(&btn2) ||
        !gpio_is_ready_dt(&btn3) || !gpio_is_ready_dt(&btn4)) {
        return -ENODEV;
    }

    // Configure all button pins as inputs
    // Note: Pull-up/pull-down configuration is typically done in devicetree
    ret |= gpio_pin_configure_dt(&btn1, GPIO_INPUT);
    ret |= gpio_pin_configure_dt(&btn2, GPIO_INPUT);
    ret |= gpio_pin_configure_dt(&btn3, GPIO_INPUT);
    ret |= gpio_pin_configure_dt(&btn4, GPIO_INPUT);

    if (ret != 0) {
        return ret;
    }

    // Read initial button states
    g_ui8ButtonStates = button_poll();

    return 0;
}

/**
 * @brief Poll the current state of all buttons
 *
 * Reads the GPIO pins for all buttons and returns their state.
 * Active LOW: bit = 0 when button is pressed, bit = 1 when released
 *
 * @return uint8_t Button state byte (bits 0-3 for buttons 1-4)
 */
uint8_t button_poll(void)
{
    uint8_t button_states = 0;
    int button1_status, button2_status, button3_status, button4_status;

    // Read all button GPIO pins
    button1_status = gpio_pin_get_dt(&btn1);
    button2_status = gpio_pin_get_dt(&btn2);
    button3_status = gpio_pin_get_dt(&btn3);
    button4_status = gpio_pin_get_dt(&btn4);

    // Build button state byte (bit = 1 when released, bit = 0 when pressed for active low)
    button_states = (button1_status & 0x1) |
                    ((button2_status & 0x1) << 1) |
                    ((button3_status & 0x1) << 2) |
                    ((button4_status & 0x1) << 3);

    g_ui8ButtonStates = button_states;

    return g_ui8ButtonStates;
}

/**
 * @brief Check if a specific button is currently pressed
 *
 * @param button_mask Button mask (e.g., BUTTON_1_MASK)
 * @return true if button is pressed (active low = 0)
 * @return false if button is released
 */
bool button_is_pressed(uint8_t button_mask)
{
    uint8_t current_state = button_poll();

    // Active low: button is pressed when bit is 0
    return !(current_state & button_mask);
}

/**
 * @brief Wait for a button press event using semaphores
 *
 * This function uses the semaphores configured in interrupt.c to wait
 * for button press interrupts. Currently only buttons 1 and 2 have
 * interrupt support configured.
 *
 * @param button_num Button number (1 or 2)
 * @param timeout_ms Timeout in milliseconds (K_FOREVER for infinite wait)
 * @return 0 if button was pressed, -ETIMEDOUT if timeout, -EINVAL if invalid button
 */
// int button_wait_press(uint8_t button_num, int32_t timeout_ms)
// {
//     int ret;
//     k_timeout_t timeout;

//     // Convert timeout to kernel timeout
//     if (timeout_ms == K_FOREVER) {
//         timeout = K_FOREVER;
//     } else {
//         timeout = K_MSEC(timeout_ms);
//     }

//     // Wait on the appropriate semaphore
//     switch (button_num) {
//         case 1:
//             ret = k_sem_take(&button1_sem, timeout);
//             break;
//         case 2:
//             ret = k_sem_take(&button2_sem, timeout);
//             break;
//         default:
//             return -EINVAL;  // Invalid button number
//     }

//     return ret;
// }





























