//*****************************************************************************
//!
//! @file interrupts.c
//! @author Anders Bandt
//! @brief Driver code for interrupts
//! @version 1.0
//! @date January 2025
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>


/* Zephyr files  */
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Semaphores for button event signaling */
K_SEM_DEFINE(button1_sem, 0, 1);
K_SEM_DEFINE(button2_sem, 0, 1);
K_SEM_DEFINE(button3_sem, 0, 1);
K_SEM_DEFINE(button4_sem, 0, 1);

/* Future interrupt flags for other peripherals */
volatile int BMS_INT_FLAG=0;
volatile int IMU_1_INT_FLAG=0;
volatile int IMU_2_INT_FLAG=0;
volatile int AFE_1_INT_FLAG=0;
volatile int AFE_2_INT_FLAG=0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const struct gpio_dt_spec btn_int1 = GPIO_DT_SPEC_GET(DT_NODELABEL(button1), gpios);
static const struct gpio_dt_spec btn_int2 = GPIO_DT_SPEC_GET(DT_NODELABEL(button2), gpios);
static const struct gpio_dt_spec btn_int3 = GPIO_DT_SPEC_GET(DT_NODELABEL(button3), gpios);
static const struct gpio_dt_spec btn_int4 = GPIO_DT_SPEC_GET(DT_NODELABEL(button4), gpios);


static struct gpio_callback btn_int1_cb;
static struct gpio_callback btn_int2_cb;
static struct gpio_callback btn_int3_cb;
static struct gpio_callback btn_int4_cb;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! ISR FUNCTIONS ---------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
*  Note: GPIO interrupts are cleared prior to invoking callbacks.
*/


static void btn_int1_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    k_sem_give(&button1_sem);
}

static void btn_int2_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    k_sem_give(&button2_sem);
}

static void btn_int3_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    k_sem_give(&button3_sem);
}

static void btn_int4_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    k_sem_give(&button4_sem);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GPIO INTERRUPT CONFIGURATION FUNCTIONS --------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 *  config_all_interrupts: configures all the interrupts for the program
 */
void config_all_interrupts()
{

    // TODO: evaluate need for having this in here
    // --- INT1 setup ---
    // Make sure that the button was initialized
    // if (!gpio_is_ready_dt(&button1)) {
    //     printk("ERROR: button not ready\r\n");
    //     return 0;
    // }

    // BTN1 setup
    // TODO: have this return a status
    int ret = 0;
    ret |= gpio_pin_configure_dt(&btn_int1, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn_int1, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&btn_int1_cb, btn_int1_handler, BIT(btn_int1.pin));
    gpio_add_callback(btn_int1.port, &btn_int1_cb);

    // BTN2 setup
    gpio_pin_configure_dt(&btn_int2, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn_int2, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&btn_int2_cb, btn_int2_handler, BIT(btn_int2.pin));
    gpio_add_callback(btn_int2.port, &btn_int2_cb);

    // BTN3 setup
    gpio_pin_configure_dt(&btn_int3, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn_int3, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&btn_int3_cb, btn_int3_handler, BIT(btn_int3.pin));
    gpio_add_callback(btn_int3.port, &btn_int3_cb);

    // BTN4 setup
    gpio_pin_configure_dt(&btn_int4, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn_int4, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&btn_int4_cb, btn_int4_handler, BIT(btn_int4.pin));
    gpio_add_callback(btn_int4.port, &btn_int4_cb);
}


