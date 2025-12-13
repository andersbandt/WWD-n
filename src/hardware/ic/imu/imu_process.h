/*
 * imu_process.h
 *
 *  Created on: Nov 21, 2023
 *      Author: ander
 */

#ifndef SRC_IC_IMU_IMU_PROCESS_H_
#define SRC_IC_IMU_IMU_PROCESS_H_


/* My header files  */
#include <circular_buffer.h>


void process_lowpass(Circular_Buffer * buffer);



#endif /* SRC_IC_IMU_IMU_PROCESS_H_ */






