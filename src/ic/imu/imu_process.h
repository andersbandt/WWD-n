/*
 * imu_process.h
 *
 *  Created on: Nov 21, 2023
 *      Author: ander
 */

#ifndef SRC_IC_IMU_IMU_PROCESS_H_
#define SRC_IC_IMU_IMU_PROCESS_H_


/* Driver Header files  */
#include <config/ti_drivers_config.h>
#include <ti/display/Display.h>


/* My header files  */
#include <src/circular_buffer.h>


void process_lowpass(Circular_Buffer * buffer, Display_Handle display);



#endif /* SRC_IC_IMU_IMU_PROCESS_H_ */






