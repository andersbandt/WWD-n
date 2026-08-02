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
#include <zephyr/kernel.h>
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


/* Buttons live on the MCP23008 over I2C, which has shown intermittent
 * boot-time failures (marginal connection, still being chased on the bench)
 * — a handful of retries lets a transient NACK/EIO clear on its own instead
 * of permanently disabling that button for the whole session. */
#define GPIO_INT_SETUP_RETRIES     5
#define GPIO_INT_SETUP_RETRY_DELAY_MS  50

static int setup_gpio_interrupt(const struct gpio_dt_spec *spec,
                                gpio_flags_t flags,
                                struct gpio_callback *cb,
                                gpio_callback_handler_t handler)
{
    int ret;
    int attempt;

    for (attempt = 1; attempt <= GPIO_INT_SETUP_RETRIES; attempt++) {
        ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
        if (ret) {
            LOG_WRN("gpio_pin_configure failed (%d), attempt %d/%d",
                    ret, attempt, GPIO_INT_SETUP_RETRIES);
            k_msleep(GPIO_INT_SETUP_RETRY_DELAY_MS);
            continue;
        }

        ret = gpio_pin_interrupt_configure_dt(spec, flags);
        if (ret) {
            LOG_WRN("gpio_pin_interrupt_configure failed (%d), attempt %d/%d",
                    ret, attempt, GPIO_INT_SETUP_RETRIES);
            k_msleep(GPIO_INT_SETUP_RETRY_DELAY_MS);
            continue;
        }

        break;
    }

    if (ret) {
        LOG_ERR("gpio_pin_configure/interrupt_configure failed (%d) after %d attempts",
                ret, GPIO_INT_SETUP_RETRIES);
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
 *
 *  Button setup failures are logged but non-fatal: buttons live on the
 *  MCP23008 GPIO expander, which is not populated on every board (see
 *  imu_notes.md / CLAUDE.md). A missing expander must not prevent the IMU
 *  interrupt (real hardware, INT1 only on this board) from being configured
 *  — button setup used to `return` immediately on failure, which meant an
 *  absent MCP23008 silently skipped IMU interrupt setup too.
 */
int config_all_interrupts(void)
{
    int ret;
    int button_ret = 0;

    /* Buttons — non-fatal, see comment above */
    ret = setup_gpio_interrupt(&btn_int1,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int1_cb,
                               btn_int1_handler);
    if (ret) { LOG_WRN("button1 interrupt setup failed (%d)", ret); button_ret = ret; }

    ret = setup_gpio_interrupt(&btn_int2,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int2_cb,
                               btn_int2_handler);
    if (ret) { LOG_WRN("button2 interrupt setup failed (%d)", ret); button_ret = ret; }

    ret = setup_gpio_interrupt(&btn_int3,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int3_cb,
                               btn_int3_handler);
    if (ret) { LOG_WRN("button3 interrupt setup failed (%d)", ret); button_ret = ret; }

    ret = setup_gpio_interrupt(&btn_int4,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &btn_int4_cb,
                               btn_int4_handler);
    if (ret) { LOG_WRN("button4 interrupt setup failed (%d)", ret); button_ret = ret; }

    if (button_ret) {
        LOG_WRN("one or more button interrupts unavailable (MCP23008 not populated?) — continuing");
    }

    /* IMU interrupts — real hardware, failure here is fatal.
     *
     * GPIO_INT_EDGE_TO_ACTIVE, not EDGE_RISING: INT1 is active-low
     * (GPIO_ACTIVE_LOW in the DTS) and pulsed by enableFifoInterrupt() —
     * EDGE_TO_ACTIVE respects the ACTIVE_LOW flag and fires on the falling
     * (assertion) edge, which is what imu_bringup.c's imu_probe() diagnostic
     * verified against the real FIFO drain rate (see imu_notes.md). This was
     * mistakenly EDGE_RISING here — a literal rising-edge trigger, which is
     * the pulse's *trailing* edge given ACTIVE_LOW, not the verified config —
     * and since GPIO interrupt-edge is one hardware setting per pin, whichever
     * of imu_bringup.c's or this setup ran last silently won. */
    ret = setup_gpio_interrupt(&imu_int1,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &imu_int1_cb,
                               imu_int1_handler);
    if (ret) return ret;

#if IMU_HAS_INT2
    ret = setup_gpio_interrupt(&imu_int2,
                               GPIO_INT_EDGE_TO_ACTIVE,
                               &imu_int2_cb,
                               imu_int2_handler);
    if (ret) return ret;
#endif

    return 0;
}



