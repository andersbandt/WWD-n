//*****************************************************************************
//!
//! @file UIFunctions.c
//! @author Anders Bandt
//! @brief This file is for defining functions that get called from the user interface
//! @version 1.0
//! @date Feburary 2022
//!
//*****************************************************************************

#ifndef SRC_UI_UIFUNCTIONS_H_
#define SRC_UI_UIFUNCTIONS_H_


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! FUNCTIONS -------------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void reset_uifunc_params();

/////////////////////////////////////////////////////
////////// MENU 0 - SYSTEM SETTINGS /////////////////
/////////////////////////////////////////////////////

/**
 * @brief UI function to walk the user through prompting for time
 */
void system_prompt_for_time_UI_FUNC();


/**
 * @brief UI function to change display contrast
 */
void system_change_display_contrast_UI_FUNC();


/**
 * @brief UI function to clear system faults
 */
void system_clear_faults_UI_FUNC(void);


/////////////////////////////////////////////////////
////////// MENU 1 - TEMPERATURE AND HUMIDITY ////////
/////////////////////////////////////////////////////

/**
 * @brief UI function to read IMU data and display it
 */
void imuRead_UI_FUNC(void);


/**
 * @brief UI function to read IMU temperature and display it
 */
void imutempRead_UI_FUNC(void);




#endif /* SRC_UI_UIFUNCTIONS_H_ */
