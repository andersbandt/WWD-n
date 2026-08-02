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
#include <zephyr/logging/log.h>

/* My header files */
#include <hardware/button.h>
#include <circular_buffer.h>

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

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

// Holds the current state of each button. A 1 in a bit indicates that
// button is currently pressed (gpio_port_get() already applies the
// GPIO_ACTIVE_LOW correction from the devicetree, so bit=1 means logically
// active/pressed here, not raw-low). All 4 buttons live on the same
// MCP23008 expander pin, so this only reflects reality right after the
// first real button_poll() call in init_buttons() - the 0x00 here is just
// a placeholder.
static uint8_t g_ui8ButtonStates = 0x00;

// Button input buffer for storing button events
static Circular_Buffer *button_input_buffer = NULL;

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
 * Reads all 4 button pins in a SINGLE gpio_port_get() call (one I2C
 * transaction to the MCP23008), not 4 separate gpio_pin_get_dt() calls.
 * The buttons previously were read one at a time, each doing its own
 * full-port I2C read - that meant up to 4 sequential I2C round-trips
 * elapsed between reading the first button and the last, so a
 * near-simultaneous two-button press (e.g. the SW3+SW4 "return to clock"
 * combo, see ui.c) could easily land with one button's read seeing it
 * pressed and another's read - a few hundred microseconds to a
 * millisecond later - seeing it already released, making the combo
 * unreliable to trigger. A single port-wide read makes all 4 bits
 * reflect the exact same instant.
 *
 * @return uint8_t Button state byte (bits 0-3 for buttons 1-4), bit=1
 *         means pressed (gpio_port_get() already applies the DT's
 *         GPIO_ACTIVE_LOW correction), bit=0 means released.
 */
uint8_t button_poll(void)
{
    gpio_port_value_t port_value;

    int ret = gpio_port_get(btn1.port, &port_value);
    if (ret != 0) {
        LOG_ERR("button_poll: gpio_port_get failed: %d", ret);
        return g_ui8ButtonStates;  // keep last known state on I2C error
    }

    uint8_t button_states = (((port_value >> btn1.pin) & 0x1) << 0) |
                             (((port_value >> btn2.pin) & 0x1) << 1) |
                             (((port_value >> btn3.pin) & 0x1) << 2) |
                             (((port_value >> btn4.pin) & 0x1) << 3);

    g_ui8ButtonStates = button_states;

    return g_ui8ButtonStates;
}

/**
 * @brief Check if a specific button is currently pressed
 *
 * @param button_mask Button mask (e.g., BUTTON_1_MASK)
 * @return true if button is pressed
 * @return false if button is released
 */
bool button_is_pressed(uint8_t button_mask)
{
    uint8_t current_state = button_poll();

    // bit=1 means pressed - see button_poll()
    return (current_state & button_mask) != 0;
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

/**
 * @brief Initialize the circular buffer for button inputs
 *
 * This function allocates and initializes a circular buffer to store
 * button events. Should be called during initialization.
 *
 * @return 0 on success, negative error code on failure
 */
int init_button_buffer(void)
{
    button_input_buffer = circular_buffer_init(16, sizeof(uint8_t));
    if (button_input_buffer == NULL) {
        LOG_ERR("Failed to initialize button input buffer");
        return -ENOMEM;
    }
    LOG_INF("Button input buffer initialized");
    return 0;
}

/**
 * @brief Push a button event to the input buffer
 *
 * Adds a button event to the circular buffer. If the buffer is full,
 * the event is silently dropped.
 *
 * @param button_event Button state byte to add to buffer
 */
void button_buffer_push(uint8_t button_event)
{
    if (button_input_buffer != NULL && button_event != 0) {
        circular_buffer_add(button_input_buffer, &button_event);
    }
}

/**
 * @brief Get the next button event from the input buffer
 *
 * Retrieves and removes the oldest button event from the circular buffer.
 * Returns 0 if the buffer is empty or not initialized.
 *
 * @return uint8_t Button state byte, or 0 if buffer is empty
 */
uint8_t get_button_event(void)
{
    uint8_t btn_event = 0;
    if (button_input_buffer != NULL && !circular_buffer_empty(button_input_buffer)) {
        circular_buffer_remove(button_input_buffer, &btn_event);
    }
    return btn_event;
}

/**
 * @brief Clear all button events from the input buffer
 *
 * Removes all pending button events from the circular buffer.
 * Does nothing if the buffer is not initialized.
 */
void button_buffer_clear(void)
{
    if (button_input_buffer != NULL) {
        circular_buffer_clear(button_input_buffer);
        LOG_DBG("Button buffer cleared");
    }
}





























