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


extern volatile int BMS_INT_FLAG;
extern volatile int IMU_1_INT_FLAG;
extern volatile int IMU_2_INT_FLAG;
extern volatile int AFE_1_INT_FLAG;
extern volatile int AFE_2_INT_FLAG;
extern volatile int BUTTON_1_INT_FLAG;
extern volatile int BUTTON_2_INT_FLAG;

/**
 * @brief configures an interrupt based on the provided parameters
 *
 * @param the interrupt port
 *
 * @param the GPIO peripheral
 *
 * @param the GPIO port
 *
 * @param the pin on the GPIO port
 *
 * @param the type of triggering for the interrupt
 *
 * @param the function pointer that will be mapped to the interrupt
 */
void configInterruptGPIO(int interrupt_port, int gpio_peripheral, int gpio_port, int gpio_pin, int interrupt_type, void(* function_map)(void));



/**
 * @brief configures all the interrupts for the program
 *
 * @param None (void).
 *
 * @return None (void).
 */
void config_all_interrupts(void);




#endif /* SRC_PERIPHERALS_INTERRUPTS_H_ */
