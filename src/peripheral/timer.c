//*****************************************************************************
//!
//! @file timer.c
//! @author Anders Bandt
//! @brief Timer management for WWD device using Zephyr RTOS
//! @version 1.0
//! @date January 2026
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

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>

/* My header files */
#include <hardware/led.h>
#include <peripheral/timer.h>


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! TIMER CALLBACK DECLARATIONS -------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Timer callback functions */
static void timer0_callback(struct k_timer *timer_id);
static void timer1_callback(struct k_timer *timer_id);
static void timer2_callback(struct k_timer *timer_id);
static void timer3_callback(struct k_timer *timer_id);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! TIMER DEFINITIONS -----------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Define Zephyr kernel timers */
K_TIMER_DEFINE(timer0, timer0_callback, NULL);
K_TIMER_DEFINE(timer1, timer1_callback, NULL);
K_TIMER_DEFINE(timer2, timer2_callback, NULL);
K_TIMER_DEFINE(timer3, timer3_callback, NULL);

/* Timer completion flags */
volatile int TIMER_1_DONE = 1;
volatile int TIMER_2_DONE = 0;
volatile int TIMER_3_DONE = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! TIMER INITIALIZATION --------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void timer_init(void) {
    /* Zephyr k_timer objects are statically defined with K_TIMER_DEFINE
     * and don't require runtime initialization. Timers are ready to use
     * after being defined.
     *
     * Timer configurations:
     * - Timer 0: LED blinking - 10 seconds periodic
     * - Timer 1: Clock update - 9 seconds periodic
     * - Timer 2: UI refresh - 1 second periodic
     * - Timer 3: Display timeout - 9 seconds one-shot
     */
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! TIMER CONTROL FUNCTIONS -----------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int timer_start(int timer_num) {
    k_timeout_t duration;
    k_timeout_t period;

    switch (timer_num) {
    case 0:
        /* Timer 0: LED blinking - 10 seconds periodic */
        duration = K_SECONDS(10);
        period = K_SECONDS(10);
        k_timer_start(&timer0, duration, period);
        break;

    case 1:
        /* Timer 1: Clock update - 9 seconds periodic */
        duration = K_SECONDS(9);
        period = K_SECONDS(9);
        k_timer_start(&timer1, duration, period);
        break;

    case 2:
        /* Timer 2: UI refresh - 1 second periodic */
        duration = K_SECONDS(1);
        period = K_SECONDS(1);
        k_timer_start(&timer2, duration, period);
        break;

    case 3:
        /* Timer 3: Display timeout - 9 seconds one-shot */
        duration = K_SECONDS(9);
        period = K_NO_WAIT;  /* One-shot: no repeat */
        k_timer_start(&timer3, duration, period);
        break;

    default:
        return 0;  /* Invalid timer number */
    }

    return 1;  /* Success */
}

int timer_stop(int timer_num) {
    switch (timer_num) {
    case 0:
        k_timer_stop(&timer0);
        break;
    case 1:
        k_timer_stop(&timer1);
        break;
    case 2:
        k_timer_stop(&timer2);
        break;
    case 3:
        k_timer_stop(&timer3);
        break;
    default:
        return 0;  /* Invalid timer number */
    }

    return 1;  /* Success */
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! TIMER CALLBACK IMPLEMENTATIONS ----------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Timer 0 callback - LED blinking
 *
 * Called every 10 seconds for LED status indication
 */
static void timer0_callback(struct k_timer *timer_id) {
    led_fast_blink(30);
}

/**
 * @brief Timer 1 callback - Clock update
 *
 * Called every 9 seconds to signal clock update needed
 */
static void timer1_callback(struct k_timer *timer_id) {
    TIMER_1_DONE = 1;
}

/**
 * @brief Timer 2 callback - UI refresh
 *
 * Called every 1 second to signal UI refresh needed
 */
static void timer2_callback(struct k_timer *timer_id) {
    TIMER_2_DONE = 1;
}

/**
 * @brief Timer 3 callback - Display timeout
 *
 * One-shot timer called after 9 seconds to signal display turn-off
 */
static void timer3_callback(struct k_timer *timer_id) {
    TIMER_3_DONE = 1;
}
