#ifndef TIMER_H
#define TIMER_H

#include <zephyr/kernel.h>

/* Timer completion flags */
extern volatile int TIMER_1_DONE;
extern volatile int TIMER_2_DONE;
extern volatile int TIMER_3_DONE;

/**
 * @brief Initialize all timers for the WWD device
 *
 * Initializes 4 timers:
 * - Timer 0: LED blinking (10 seconds)
 * - Timer 1: Clock update (9 seconds)
 * - Timer 2: UI refresh (1 second)
 * - Timer 3: Display timeout (9 seconds, one-shot)
 */
void timer_init(void);

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
