/*
 * imu_process.c
 *
 *  Created on: Nov 21, 2023
 *      Author: ander
 */


///* Standard C99 stuff */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>



/* My header files  */
#include <circular_buffer.h>




//// define variables for pre-processing raw data
uint16_t x_m1=0;
uint16_t x_0=0;

uint16_t y_m1=0;
uint16_t y_0=0;


// LOW PASS FILTER
float a1 = 0.1;



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////! -----------------------------------------------------------------------------------------------------------------------//
////! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
////! -----------------------------------------------------------------------------------------------------------------------//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void process_lowpass(Circular_Buffer * buffer) {
    bool status = 1;
    while (status) {
//        x_0 = circular_buffer_remove(buffer);

        // the "magic"
//        y_0 = (1 - a1) * y_m1 + a1*(x_0 + x_m1)/2;
//
//        // reset variable
//        x_m1 = x_0;
//        y_m1=y_0;

        // check if buffer has been emptied
            // Verify that the buffer is empty. If it is not empty
            // return the circular buffer to the heap and return false
            if (!circular_buffer_empty(buffer)) {
//                circular_buffer_delete(test_buffer);
                status = 0;
            }
    }
}


