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

#include <ui.h>  // for ui_mode_t

// define some menu constants
#define UI_MAIN_MENU_ITEMS    6  // total amount of items in the menu (for tracking absolute position)
#define UI_MENU_ITEMS_PAGE    4  // total amount of items per page
#define SUB_MENU_MAX_LENGTH    6  // bumped for menu 0's 6th item (Bluetooth); see sub_menu_0
#define SUB_MENU_CHAR_LENGTH  22

#define START_X               12  // how far rightward to start printing lines on x axis. Used to be 8


// tracking variables
extern int abs_position;
extern int sub_menu_position;

// Sub-menu mode mappings
extern ui_mode_t sub_menu_modes[UI_MAIN_MENU_ITEMS][SUB_MENU_MAX_LENGTH];



/**
 * @brief This method initializes variables for menu operation
 *
 * @returns None (void)
 */
void initMenu();


/**
 * @brief This method returns whether a sub menu function is currently running
 * 
 * @returns true if a sub menu function is running, false otherwise
 */
bool get_running_state();


/**
 * @brief This method returns whether we are currently browsing a sub-menu's
 * item list (as opposed to the top-level main menu list)
 *
 * @returns true if browsing a sub-menu list, false if at the main menu list
 */
bool get_in_sub_menu_state();


/**
 * @brief This method handles updating the UI screen based on a new user action
 *
 * @param Represents what action occurred
 *                      -1 represents a move "up" in the menu
 *                      +1 represents a move "down"
 *                      +2 represents a "select" action
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
 * @param force If set, blanks the entire cursor column instead of just
 *              erasing prev_position - use whenever prev_position may be
 *              stale state from a different screen (e.g. entering/leaving
 *              a submenu), not just an incremental move on the same page.
 *
 * @returns None (void)
 */
void updateCursor(int prev_position, int position, int force);


/**
 * @brief returns one menu level up in the UI
 *
 * @returns None (void)
 */
void returnMenu();


/**
 * @brief returns one menu level up in the UI
 *
 * @returns None (void)
 */
void returnSubMenu();


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


/**
 * @brief Force-exits any running sub menu function and clears menu state.
 *
 * run_sub_menu otherwise latches true forever once any sub menu action is
 * selected (nothing ever clears it) which blocks change_ui_mode(UI_MODE_CLOCK)
 * via get_running_state() — call this before returning home (e.g. the
 * SW3+SW4 "always home" combo) so the escape hatch actually works.
 *
 * @returns None (void)
 */
void ui_menu_force_exit(void);


/**
 * @brief Exits a running sub-menu function back to the sub-menu list it was
 * launched from (one level up). See ui_menu.c for the full contract - the
 * caller must still set ui_mode = UI_MODE_MENU itself, directly rather than
 * via change_ui_mode().
 *
 * @returns None (void)
 */
void ui_menu_return_to_sub_menu(void);




#endif /* SRC_UI_UI_MENU_H_ */
