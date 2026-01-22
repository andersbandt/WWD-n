//*****************************************************************************
//!
//! @file interrupts.h
//! @author Anders Bandt
//! @brief Function prototypes for interrupt control
//! @version 1.0
//! @date January 2022
//!
//*****************************************************************************

#ifndef SRC_PERIPHERALS_INTERRUPTS_H_
#define SRC_PERIPHERALS_INTERRUPTS_H_

#include <zephyr/kernel.h>

/* Button semaphores for thread synchronization */
extern struct k_sem button1_sem;
extern struct k_sem button2_sem;
extern struct k_sem button3_sem;
extern struct k_sem button4_sem;

/* IMU interrupt semaphores */
extern struct k_sem imu_int1_sem;
extern struct k_sem imu_int2_sem;


/**
 * @brief configures all the interrupts for the program
 *
 * @param None (void).
 *
 * @return None (void).
 */
int config_all_interrupts(void);


#endif /* SRC_PERIPHERALS_INTERRUPTS_H_ */
