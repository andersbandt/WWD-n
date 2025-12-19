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

volatile int BMS_INT_FLAG=0;
volatile int IMU_1_INT_FLAG=0;
volatile int IMU_2_INT_FLAG=0;
volatile int AFE_1_INT_FLAG=0;
volatile int AFE_2_INT_FLAG=0;
volatile int BUTTON_1_INT_FLAG;
volatile int BUTTON_2_INT_FLAG;



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* The devicetree node identifier for the "led0" alias. */
// #define IMU_INT1_NODE DT_ALIAS(imu_int1)
// #define IMU_INT2_NODE DT_ALIAS(imu_int2)


// static const struct gpio_dt_spec imu_int1 = GPIO_DT_SPEC_GET(IMU_INT1_NODE, gpios);
// static const struct gpio_dt_spec imu_int2 = GPIO_DT_SPEC_GET(IMU_INT2_NODE, gpios);


static const struct gpio_dt_spec btn_int1 = GPIO_DT_SPEC_GET(DT_NODELABEL(button1), gpios);
static const struct gpio_dt_spec btn_int2 = GPIO_DT_SPEC_GET(DT_NODELABEL(button2), gpios);
static const struct gpio_dt_spec btn_int3 = GPIO_DT_SPEC_GET(DT_NODELABEL(button3), gpios);
static const struct gpio_dt_spec btn_int4 = GPIO_DT_SPEC_GET(DT_NODELABEL(button4), gpios);


static struct gpio_callback btn_int1_cb;
static struct gpio_callback btn_int2_cb;


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
    BUTTON_1_INT_FLAG = 1;
}

static void btn_int2_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    BUTTON_2_INT_FLAG = 1;
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

    // --- INT1 setup ---
    // Make sure that the button was initialized
    // if (!gpio_is_ready_dt(&button1)) {
    //     printk("ERROR: button not ready\r\n");
    //     return 0;
    // }

    int ret = 0;
    ret |= gpio_pin_configure_dt(&btn_int1, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn_int1, GPIO_INT_EDGE_TO_ACTIVE);

    // configure callback
    gpio_init_callback(&btn_int1_cb, btn_int1_handler, BIT(btn_int1.pin));
    gpio_add_callback(btn_int1.port, &btn_int1_cb);

    // --- INT2 setup ---
    gpio_pin_configure_dt(&btn_int2, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn_int2, GPIO_INT_EDGE_TO_INACTIVE);

    gpio_init_callback(&btn_int2_cb, btn_int2_handler, BIT(btn_int2.pin));
    gpio_add_callback(btn_int2.port, &btn_int2_cb);
}


