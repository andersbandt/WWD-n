#ifndef TIMER_H
#define TIMER_H

#include <zephyr/kernel.h>

/* Timer semaphores for thread synchronization */
extern struct k_sem timer1_sem;  /* Clock update semaphore */
extern struct k_sem timer2_sem;  /* UI refresh semaphore */
extern struct k_sem timer3_sem;  /* Display timeout semaphore */
extern struct k_sem timer4_sem;  /* Display timeout semaphore */

/**
 * @brief Initialize all timers for the WWD device
 *
 * Initializes 4 timers:
 * - Timer 0: LED blinking (10 seconds)
 * - Timer 1: Clock update (9 seconds)
 * - Timer 2: UI refresh (1 second)
 * - Timer 3: Display timeout (9 seconds, one-shot)
 */
void init_timer(void);

/**
 * @brief Start a specific timer
 *
 * @param timer_number Timer to start (0-3)
 * @return 0 on error, 1 on success
 */
int timer_start(int timer_number);

/**
 * @brief Stop a specific timer
 *
 * @param timer_number Timer to stop (0-3)
 * @return 0 on error, 1 on success
 */
int timer_stop(int timer_number);

#endif
