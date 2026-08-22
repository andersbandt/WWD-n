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

// need to be placed before timer definitions

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



// NOTE: this has to be here (for at least TIMER0)
// All timer periods, in one place (2026-08-22 — was scattered as inline literals
// in timer_start()'s switch and the timer0 callback below; timer1 and timer3 in
// particular shared the same "9" written as two separate literals, easy to
// change one while meaning the other).
#define TIMER0_PERIOD       5   /* LED blink group repeat, seconds */
#define TIMER0_BLINK_MSEC   50  /* fast on/off toggle rate within one blink burst */
#define TIMER1_PERIOD       9   /* sensor (IMU temp/step-count) update, seconds */
#define TIMER2_PERIOD       1   /* UI refresh / wall-clock tick, seconds */
#define TIMER3_PERIOD       9   /* display timeout, seconds (one-shot; timer currently
                                 * never started — see main.c/CLAUDE.md Known Issues) */


/* Semaphores for thread synchronization */
K_SEM_DEFINE(timer1_sem, 0, 1);  /* Sensor (IMU temp/step-count) update semaphore */
K_SEM_DEFINE(timer2_sem, 0, 1);  /* UI refresh semaphore (also drives wall-clock update) */
K_SEM_DEFINE(timer3_sem, 0, 1);  /* Display timeout semaphore */


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! TIMER INITIALIZATION --------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void init_timer(void) {
    timer_start(0);  /* Start LED blink timer */
    timer_start(1);
    timer_start(2);
    // timer_start(3);
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
        /* Timer 0: LED blinking */
        duration = K_SECONDS(TIMER0_PERIOD);
        period = K_SECONDS(TIMER0_PERIOD);
        k_timer_start(&timer0, duration, period);
        break;
    case 1:
        /* Timer 1: Sensor (IMU temp/step-count) update */
        duration = K_SECONDS(TIMER1_PERIOD);
        period = K_SECONDS(TIMER1_PERIOD);
        k_timer_start(&timer1, duration, period);
        break;

    case 2:
        /* Timer 2: UI refresh (also drives wall-clock update) */
        duration = K_SECONDS(TIMER2_PERIOD);
        period = K_SECONDS(TIMER2_PERIOD);
        k_timer_start(&timer2, duration, period);
        break;

    case 3:
        /* Timer 3: Display timeout - one-shot */
        duration = K_SECONDS(TIMER3_PERIOD);
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
 * Called for LED status indication
 */
static int blink_count = 0;
static int blink_state = 1;
static void timer0_callback(struct k_timer *timer_id) {
    led_set(1, blink_state);
    blink_count++;
    blink_state = !blink_state;
    
    if (blink_count >= 8) {  // divide number by 2 to get total number of blinks
        blink_count = 0;
        led_set(1, 0); // make sure the LED is off
        k_timer_start(&timer0, K_SECONDS(TIMER0_PERIOD), K_SECONDS(TIMER0_PERIOD));
    }
    else {
        k_timer_start(&timer0, K_MSEC(TIMER0_BLINK_MSEC), K_MSEC(TIMER0_BLINK_MSEC));
    }
}


/**
 * @brief Timer 1 callback - Clock update
 *
 * Called every 9 seconds to signal clock update needed
 */
static void timer1_callback(struct k_timer *timer_id) {
    k_sem_give(&timer1_sem);
}

/**
 * @brief Timer 2 callback - UI refresh
 *
 * Called every 1 second to signal UI refresh needed
 */
static void timer2_callback(struct k_timer *timer_id) {
    k_sem_give(&timer2_sem);
}

/**
 * @brief Timer 3 callback - Display timeout
 *
 * One-shot timer called after 9 seconds to signal display turn-off
 */
static void timer3_callback(struct k_timer *timer_id) {
    k_sem_give(&timer3_sem);
}
