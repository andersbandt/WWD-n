//*****************************************************************************
//!
//! @file button.h
//! @author Anders Bandt
//! @brief This header file is for controller user interface functions such as rotary encoder, buttons, displays, etc.
//! @version 1.0
//! @date January 2022
//!
//*****************************************************************************

#ifndef INTERFACECONTROL_H_
#define INTERFACECONTROL_H_



#include <stdint.h>



void init_buttons();



uint8_t button_poll();





#endif /* INTERFACECONTROL_H_ */


