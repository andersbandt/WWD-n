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
 * @brief UI function to adjust the display backlight brightness.
 * SW1 (top-left) decrements, SW4 (bottom-left) increments — same
 * left-column-changes-the-value convention as the time/date setter.
 */
void system_adjust_brightness_UI_FUNC(void);


/**
 * @brief UI function to clear system faults
 */
void system_low_power_UI_FUNC(void);


/** @brief Bluetooth on/off toggle screen (UI_MODE_BLE). UP = on, DOWN = off. */
void system_ble_UI_FUNC(void);
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


/**
 * @brief UI function to display the current step count
 */
void pedometer_UI_FUNC(void);


/**
 * @brief UI function to plot the in-RAM recent temperature history (see
 * temp_history_push()/temp_history_get() in imu.c) as a line graph
 */
void tempGraph_UI_FUNC(void);

/* Battery voltage over the last day (one stored point per 5 minutes) and
 * steps per hour for today. Both live under the Graphs menu; see the function
 * comments in UIFunctions.c for why one is a line and the other bars. */
void batteryGraph_UI_FUNC(void);
void stepsGraph_UI_FUNC(void);


/////////////////////////////////////////////////////
////////// MENU 2 - DATA ////////////////////////////
/////////////////////////////////////////////////////

/**
 * @brief UI function to display NVS log stats (write offset, meta seq)
 */
void data_stats_UI_FUNC(void);


/*
 * erase_flash_UI_FUNC: on-device chip erase, behind a confirm screen whose
 * cursor starts on Cancel. Committing requires UP/DOWN then SELECT, so no
 * repeated press on a single button can trigger it. Also wipes the rate
 * config, which lives on the same chip.
 */
void erase_flash_UI_FUNC(void);


/////////////////////////////////////////////////////
////////// MENU 3 - TIMER ///////////////////////////
/////////////////////////////////////////////////////

/**
 * @brief UI function for a start/pause/reset stopwatch.
 * SW1 (BUTTON_1_MASK) toggles start/pause, SW4 (BUTTON_4_MASK) resets to 0.
 */
void stopwatch_UI_FUNC(void);


/**
 * @brief Activity session screen (UI_MODE_ACTIVITY).
 *
 * Lists the activity catalogue from activity.c. UP/DOWN move the cursor,
 * SELECT starts or stops the highlighted activity, writing a RECORD_ACTIVITY
 * marker into the log. BACK is handled generically by handle_ui_input().
 */
void activity_UI_FUNC(void);


#endif /* SRC_UI_UIFUNCTIONS_H_ */
