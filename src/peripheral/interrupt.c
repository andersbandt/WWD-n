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
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(interrupt, LOG_LEVEL_INF);

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

/* Semaphores for IMU interrupt signaling */
K_SEM_DEFINE(imu_int1_sem, 0, 1);
K_SEM_DEFINE(imu_int2_sem, 0, 1);

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

static const struct gpio_dt_spec imu_int1 = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(icm42670p), int_gpios, 0);
#define IMU_HAS_INT2 (DT_PROP_LEN(DT_NODELABEL(icm42670p), int_gpios) > 1)
#if IMU_HAS_INT2
static const struct gpio_dt_spec imu_int2 = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(icm42670p), int_gpios, 1);
#endif

static struct gpio_callback btn_int1_cb;
static struct gpio_callback btn_int2_cb;
static struct gpio_callback btn_int3_cb;
static struct gpio_callback btn_int4_cb;

static struct gpio_callback imu_int1_cb;
#if IMU_HAS_INT2
static struct gpio_callback imu_int2_cb;
#endif

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

static void imu_int1_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    k_sem_give(&imu_int1_sem);
}

#if IMU_HAS_INT2
static void imu_int2_handler(const struct device *dev,
                             struct gpio_callback *cb,
                             uint32_t pins)
{
    k_sem_give(&imu_int2_sem);
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GPIO INTERRUPT CONFIGURATION FUNCTIONS --------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int setup_gpio_interrupt(const struct gpio_dt_spec *spec,
                                gpio_flags_t flags,
                                struct gpio_callback *cb,
                                gpio_callback_handler_t handler)
{
    int ret;

    ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
    if (ret) {
        LOG_ERR("gpio_pin_configure failed (%d)", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(spec, flags);
    if (ret) {
        LOG_ERR("gpio_pin_interrupt_configure failed (%d)", ret);
        return ret;
    }

    gpio_init_callback(cb, handler, BIT(spec->pin));

    ret = gpio_add_callback(spec->port, cb);
    if (ret) {
        LOG_ERR("gpio_add_callback failed (%d)", ret);
        return ret;
    }

    return 0;
}


/*
 *  config_all_interrupts: configures all the interrupts for the program
 */
int config_all_interrupts(void)
{
    int ret;

    /* Buttons */
    ret = setup_gpio_interrupt(&btn_int1,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int1_cb,
                               btn_int1_handler);
    if (ret) return ret;

    ret = setup_gpio_interrupt(&btn_int2,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int2_cb,
                               btn_int2_handler);
    if (ret) return ret;

    ret = setup_gpio_interrupt(&btn_int3,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int3_cb,
                               btn_int3_handler);
    if (ret) return ret;

    ret = setup_gpio_interrupt(&btn_int4,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int4_cb,
                               btn_int4_handler);
    if (ret) return ret;

    /* IMU interrupts */
    ret = setup_gpio_interrupt(&imu_int1,
                               GPIO_INT_EDGE_RISING,
                               &imu_int1_cb,
                               imu_int1_handler);
    if (ret) return ret;

#if IMU_HAS_INT2
    ret = setup_gpio_interrupt(&imu_int2,
                               GPIO_INT_EDGE_RISING,
                               &imu_int2_cb,
                               imu_int2_handler);
    if (ret) return ret;
#endif

    return 0;
}



