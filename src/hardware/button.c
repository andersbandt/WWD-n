//*****************************************************************************
//!
//! @file button.c
//! @author Anders Bandt
//! @brief Function descriptions for driving buttons
//! @version 1.0
//! @date August 11th, 2024
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Zephyr files  */
#include <zephyr/drivers/gpio.h>

/* My header files */
#include <hardware/button.h>



volatile int BUTTON_1_FLAG=0;
volatile int BUTTON_2_FLAG=0;


// Holds the current, debounced state of each button.  A 0 in a bit indicates
// that that button is currently pressed, otherwise it is released.
// We assume that we start with all the buttons released (though if one is
// pressed when the application starts, this will be detected).// Holds the current, debounced state of each button.  A 0 in a bit indicates
// that that button is currently pressed, otherwise it is released.
// We assume that we start with all the buttons released (though if one is
// pressed when the application starts, this will be detected).
static uint8_t g_ui8ButtonStates = 0;



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*
 *  Initializes the GPIO pins used by the board pushbuttons.

 This function must be called during application initialization to
 configure the GPIO pins to which the pushbuttons are attached.  It enables
 the port used by the buttons and configures each button GPIO as an input
 with a weak pull-up.

@ return None.
 */
void init_buttons() {

}



/**
 * button_poll: releases the ACTIVE LOW output of up to 8 buttons
 */
uint8_t button_poll()
{
    // int button1_status = GPIO_read(CONFIG_GPIO_AFE_GPIO);
    // int button2_status = GPIO_read(CONFIG_GPIO_AFE_RDY);

    // TODO: add some debouncing here? Check Hidden-Hydroponics code
    
    
    // g_ui8ButtonStates = (button1_status & 0x1) | ((button2_status & 0x1) << 1);
        
    // return g_ui8ButtonStates;
}





























