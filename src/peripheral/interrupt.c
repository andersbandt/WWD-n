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

static const struct gpio_dt_spec imu_int1 =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), IMU_INT1_gpios);

static const struct gpio_dt_spec imu_int2 =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), IMU_INT2_gpios);



static struct gpio_callback imu_int1_cb;
static struct gpio_callback imu_int2_cb;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! ISR FUNCTIONS ---------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
*  Note: GPIO interrupts are cleared prior to invoking callbacks.
*/


static void imu_int1_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    printk("IMU INT1 fired\n");
}

static void imu_int2_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    printk("IMU INT2 fired\n");
}




/*
 *  bms_Fxn: callback function for BMS interrupt
 */
/* void bmsFxn(uint_least8_t index) */
/* { */
/*     BMS_INT_FLAG = 1; */
/*     return; */
/* } */


/* /\* */
/*  *  imu1Fxn: callback function for IMU interrupt #1 */
/*  *\/ */
/* void imu1Fxn(uint_least8_t index) */
/* { */
/*     IMU_1_INT_FLAG = 1; */
/*     return; */
/* } */


/* /\* */
/*  *  imu2Fxn: callback function for IMU interrupt #2 */
/*  *\/ */
/* void imu2Fxn(uint_least8_t index) */
/* { */
/*     IMU_2_INT_FLAG = 1; */
/*     return; */
/* } */


/* /\* */
/*  *  afe1Fxn: callback function for AFE interrupt #1 */
/*  *\/ */
/* void afe1Fxn(uint_least8_t index) */
/* { */
/*     AFE_1_INT_FLAG = 1; */
/*     return; */
/* } */


/* /\* */
/*  *  afe2Fxn: callback function for AFE interrupt #2 */
/*  *\/ */
/* void afe2Fxn(uint_least8_t index) */
/* { */
/*     AFE_2_INT_FLAG = 1; */
/*     return; */
/* } */


/* /\* */
/*  *  but1Fxn: callback function for button 1 */
/*  *\/ */
/* void but1Fxn(uint_least8_t index) */
/* { */
/*     BUTTON_1_INT_FLAG= 1; */
/*     return; */
/* } */


/* /\* */
/*  *  but1Fxn: callback function for button 1 */
/*  *\/ */
/* void but2Fxn(uint_least8_t index) */
/* { */
/*     BUTTON_2_INT_FLAG= 1; */
/*     return; */
/* } */



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GPIO INTERRUPT CONFIGURATION FUNCTIONS --------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*!
 *  @brief      Bind a callback function to a GPIO pin interrupt
 *
 *  Associate a callback function with a particular GPIO pin interrupt.
 *
 *  Callbacks can be changed at any time, making it easy to switch between
 *  efficient, state-specific interrupt handlers.
 *
 *  Note: The callback function is called within the context of an interrupt
 *  handler.
 *
 *  Note: This API does not enable the GPIO pin interrupt.
 *  Use GPIO_enableInt() and GPIO_disableInt() to enable and disable the pin
 *  interrupt as necessary, or use GPIO_CFG_INT_ENABLE when calling setConfig.
 *
 *  Note: it is not necessary to call GPIO_clearInt() within a callback.
 *  That operation is performed internally before the callback is invoked.
 *
 *  @param      dio_n       GPIO index
 *  @param      callback    address of the callback function
 */
/* void configInterruptGPIO(int dio_n, int polarity, GPIO_CallbackFxn callback) */
/* { */
/*     // calling below config just to be sure but can phase out */
/*     if (polarity) { */
/*         GPIO_setConfig(dio_n, GPIO_CFG_IN_PD | GPIO_CFG_IN_INT_RISING); */
/*         //    GPIO_setConfig(dio_n, GPIO_CFG_IN_NOPULL | GPIO_CFG_IN_INT_RISING); */
/*     } */
/*     else { */
/*         GPIO_setConfig(dio_n, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING); */
/*     } */

/*     /\* Install Button callback *\/ */
/*     GPIO_setCallback(dio_n, callback); */

/*     /\* Enable interrupts *\/ */
/*     GPIO_enableInt(dio_n); */

/*     return; */
/* } */



/*
 *  config_all_interrupts: configures all the interrupts for the program
 */
void config_all_interrupts()
{
    ////  BMS_INTERRUPT  ////
    /* configInterruptGPIO(CONFIG_GPIO_BMS_INT, 1, bmsFxn); */

    /* ////  IMU_INTERRUPT //// */
    /* configInterruptGPIO(CONFIG_GPIO_IMU_INT_1, 1, imu1Fxn); */
    /* configInterruptGPIO(CONFIG_GPIO_IMU_INT_2, 1, imu2Fxn); */

    /* ////  AFE_INTERRUPT //// */
    /* /\* configInterruptGPIO(CONFIG_GPIO_AFE_RDY_CONST, afe1Fxn); *\/ */
    /* /\* configInterruptGPIO(CONFIG_GPIO_AFE_GPIO_CONST, afe2Fxn); *\/ */

    /* configInterruptGPIO(CONFIG_GPIO_AFE_RDY, 0, but1Fxn); */
    /* configInterruptGPIO(CONFIG_GPIO_AFE_GPIO, 0, but2Fxn); */

        // --- INT1 setup ---
    gpio_pin_configure_dt(&imu_int1, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(&imu_int1_cb, imu_int1_handler, BIT(imu_int1.pin));
    gpio_add_callback(imu_int1.port, &imu_int1_cb);

    // --- INT2 setup ---
    gpio_pin_configure_dt(&imu_int2, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&imu_int2, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(&imu_int2_cb, imu_int2_handler, BIT(imu_int2.pin));
    gpio_add_callback(imu_int2.port, &imu_int2_cb);

    printk("GPIO interrupts configured\n");
}


