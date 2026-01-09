//*****************************************************************************
//!
//! @file ui_menu.h
//! @author Anders Bandt
//! @brief Header file for all things related to menu items (not aptly named, more than just sub menu)
//! @version 1.0
//! @date January 2022
//!
//*****************************************************************************

#ifndef SRC_UI_UI_MENU_H_
#define SRC_UI_UI_MENU_H_


// define some menu constants
#define UI_MAIN_MENU_ITEMS    4  // total amount of items in the menu (for tracking absolute position)
#define UI_MENU_ITEMS_PAGE    4  // total amount of items per page
#define SUB_MENU_MAX_LENGTH    4
#define SUB_MENU_CHAR_LENGTH  22

#define START_X               12  // how far rightward to start printing lines on x axis. Used to be 8


// tracking variables
extern volatile int abs_position;
extern volatile int sub_menu_position;

extern void (*sub_menu_options[UI_MAIN_MENU_ITEMS][SUB_MENU_MAX_LENGTH])();



/**
 * @brief This method initializes variables for menu operation
 *
 * @returns None (void)
 */
void initMenu();


/**
 * @brief This method handles updating the UI screen based on a new user action
 *
 * @param Represents what action occurred. +2 represents a move "up" in the menu where +1 represents a move "down". A 0 represents a select
 * button being pressed
 *
 * @returns None (void)
 */
void updateMenuScreen(int8_t action);


/**
 * @brief Handles updating the main menu screen based on new absolute position
 *
 *
 * @returns None (void)
 */
void updateMainMenuScreen(int absolute_position, int force);


/**
 * @brief Handles updating the sub menu screen
 *
 * @param is the absolute position in the menu (determines what sub menu to load)
 *
 * @param is the sub menu position (determines what options within the sub menu we are examining)
 *
 * @returns None (void)
 */
void updateSubMenuScreen(int abs_pos, int sub_pos, int force);


/**
 * @brief Updates the cursor location on the screen
 *
 * @param[in] prev_position previous location of the cursor
 *
 * @param position The position of the cursor on the screen.
 *
 * @returns None (void)
 */
void updateCursor(int prev_position, int position);


/**
 * @brief returns one menu level up in the UI
 *
 * @returns None (void)
 */
void returnMenu();


/**
 * @brief commences a certain UI action
 *
 * @param The absolute position we are currently at
 *
 * @param The sub menu position we are currently at
 *
 * @returns None (void)
 */
void commenceUIAction(int absolute_position, int sub_menu_position);


////////////////////////////////////////////////////////////////////////
////////  HELPER LOGIC FUNCTIONS    ////////////////////////////////////
////////////////////////////////////////////////////////////////////////

/**
 * @brief changes position based on input direction
 *
 * @param action should represent the direction we are moving. +2 for "up", and +1 for "down". A 0 (for select) should not be sent to this function
 *
 * @returns None (void)
 */
void changeMainMenuPosition(int action);


/**
 * @brief changeSubMenuPosition
 *
 * @param action should represent the direction we are moving. +2 for "up", and +1 for "down". A 0 (for select) should not be sent to this function
 *
 * @returns None (void)
 */
void changeSubMenuPosition(int action);


/**
 * @brief returns if we are currently running a sub menu item
 *
 * @param None
 *
 * @returns bool representing state
 */
bool get_running_state();

/**
 * @brief getSubMenuLength
 *
 * @param The menu number we are examining (0, 1, 2, ... and so on)
 *
 * @returns None (void)
 */
int getSubMenuLength(int menu_num);










#endif /* SRC_UI_UI_MENU_H_ */
