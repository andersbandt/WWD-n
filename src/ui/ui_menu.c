//*****************************************************************************
//!
//! @file ui_menu.c
//! @author Anders Bandt
//! @brief Main code for controlling a UI menu on a display (OLED)
//! @version 1.0
//! @date January 2022
//!
//*****************************************************************************

/* Standard C99 stuff */
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* My header files */
#include <display.h> // TODO: for complete abstraction, shouldn't this not be included here?
#include <ui_menu.h>
#include <ui_display.h>
#include <UIFunctions.h>


LOG_MODULE_REGISTER(ui_menu, LOG_LEVEL_INF);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL VARIABLES ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// variable for tracking position. Defined in `ui_menu.h`
int abs_position;
int sub_menu_position;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL VARIABLES -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// main menu options
char * main_menu_options[UI_MAIN_MENU_ITEMS] = {
    "System Settings",
    "IMU",
    "Data",
    "Timer"
};


// menu 0 sub-menu options: System Settings
#define SUB_MENU_0_LENGTH     4
char sub_menu_0[SUB_MENU_0_LENGTH][SUB_MENU_CHAR_LENGTH] = {
                         "Change Date/Time",
                         "Display Settings",
                         "Clear faults",
                         "Return"
                     };


// menu 1 sub-menu options: IMU
#define SUB_MENU_1_LENGTH     4
char sub_menu_1[SUB_MENU_1_LENGTH][SUB_MENU_CHAR_LENGTH] = {
    "Display readings",
    "Temperature",
    "Pedometer",
    "Return"
                     };


// creating array of sub menu text items
char * sub_menu[UI_MAIN_MENU_ITEMS] = {*sub_menu_0, *sub_menu_1};


// Sub-menu UI mode mappings
// Maps menu positions to ui_mode_t values
// UI_MODE_MENU is used for "Return" options
ui_mode_t sub_menu_modes[UI_MAIN_MENU_ITEMS][SUB_MENU_MAX_LENGTH] = {
    // System settings (Menu 0)
    {
        UI_MODE_PROMPT_TIME,        // Change Date/Time
        UI_MODE_CHANGE_CONTRAST,    // Display Settings
        UI_MODE_CLEAR_FAULTS,       // Clear faults
        UI_MODE_MENU                // Return (back to menu)
    },

    // IMU (Menu 1)
    {
        UI_MODE_IMU_READ,           // Display readings
        UI_MODE_IMU_TEMP,           // Temperature
        UI_MODE_MENU,               // Pedometer (placeholder - returns to menu)
        UI_MODE_MENU                // Return
    },
};



// other local tracking variables
int prev_pos;
bool in_sub_menu = 0;
bool run_sub_menu = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void initMenu() {
    abs_position = 0;
    sub_menu_position = 0;
}


/*
 * updateMenuScreen: updates the screen based on new position
 *
 * action can be a 2, 1, or -1.
 * 2 is the select button being pressed
 * 1 is a movement down
 * -1 is a movement up
 */
void updateMenuScreen(int8_t action)
{
    // if we are already in a sub menu
    if (in_sub_menu) {

        // if sub menu function already running ...
        if (run_sub_menu) {
            // Check if user wants to exit the UI function
            if (action == 2) {
                run_sub_menu = false;
                reset_uifunc_params(); // NOTE: this has to be called to wipe things like position for setting clock
                returnMenu(); // TODO: this should actually return to sub menu instead of main menu
                change_ui_mode(UI_MODE_MENU); // Return to menu mode
                return;
            }
            // UI function is still running - ui_refresh() handles calling it
            return;
        }

        // if select button was pressed
        else if (action == 2) {
            run_sub_menu = true;
            k_usleep(1000*200);
            commenceUIAction(abs_position, sub_menu_position); // Set the UI mode
            return;
        }

        // handle up or down button (and any other action input)
        else {
            changeSubMenuPosition(action);
            updateSubMenuScreen(abs_position, sub_menu_position, 0);
            return;
        }
    }

    // main menu updates
    // TODO: really for massive code complexity improvements i need to make the following logic more concise
    else {  // if we are not in a sub menu
        if (action == 2) {  // if the select button was pressed
            in_sub_menu = 1;
            sub_menu_position = 0;  // might not be needed, added during debugging
            updateSubMenuScreen(abs_position, sub_menu_position, 1);
        }
        else {
            changeMainMenuPosition(action);
            updateMainMenuScreen(abs_position, 0);
        }
    }
}


/*
 * updateMainMenuScreen: handles updating the main menu page based on new position. Typically called by updateScreen()
 */
void updateMainMenuScreen(int absolute_pos, int force)
{
    // only update if we are going to a new page or 'force' is set
    if ((absolute_pos / UI_MENU_ITEMS_PAGE != prev_pos / UI_MENU_ITEMS_PAGE) || force) {  // only update if we are going to a new page

        // clear_out_display();
        int page_num = absolute_pos / UI_MENU_ITEMS_PAGE;

        int i;
        for(i = 0; i < UI_MENU_ITEMS_PAGE; i++) {
            if (i == UI_MAIN_MENU_ITEMS) {
                break;
            }
            clearAndPrintLine(main_menu_options[page_num*UI_MENU_ITEMS_PAGE + i], i+1, START_X, FONT_MEDIUM);
        }
    }

    in_sub_menu = 0;
    updateCursor(prev_pos, absolute_pos);
}


// TODO: code improvement? Combine this function with updateMainMenuScreen ... 
/*
 * updateSubMenuScreen: updates the sub menu screen based on absolute position (menu number) and sub menu position
 *
 * absolute_pos and sub_menu_pos start at 0
 */
void updateSubMenuScreen(int abs_pos, int sub_pos, int force)
{
    int page_num = sub_pos / UI_MENU_ITEMS_PAGE;  // tracks what page we are on (0, 1, 2, etc.)

    // grab how many options need to be displayed
    int num_to_display = getSubMenuLength(abs_pos) - UI_MENU_ITEMS_PAGE*page_num;

    if (num_to_display > UI_MENU_ITEMS_PAGE) {
        num_to_display = UI_MENU_ITEMS_PAGE;
    }

    // only update on new page or 'force' condition
    if ((sub_pos / UI_MENU_ITEMS_PAGE != prev_pos / UI_MENU_ITEMS_PAGE) || force) {

        int page_addition = page_num * UI_MENU_ITEMS_PAGE;

        // print lines of the sub menu
        int i;
        for(i = 0; i < num_to_display; i++) {
            clearAndPrintLine(sub_menu[abs_pos] + 22*(i+page_addition), i+1, START_X, FONT_MEDIUM);
        }
    }

    // update the cursor - only to the point where menu items exist
    if (sub_pos % UI_MENU_ITEMS_PAGE < num_to_display) {
        updateCursor(prev_pos, sub_pos);
    }
    else {
        updateCursor(prev_pos, num_to_display-1);
    }
}


/*
 * updateCursor: updates the cursor (">") position
 */
void updateCursor(int prev_position, int position)
{
   // calculate positions relative to page
   int relative_position = position % UI_MENU_ITEMS_PAGE;  // generate the 'relative position' with modulus division
   int prev_relative_position = prev_position % UI_MENU_ITEMS_PAGE;

   // TODO: is there a better way to perform this erasing?
   printLine(" ", prev_relative_position+1, 0, FONT_MEDIUM); // erase old cursor
   printLineTransparent("x", relative_position+1, 0, FONT_MEDIUM); // draw new cursor
}


/*
 * returnMenu: returns one level up in the UI menu system
 */
void returnMenu()
{
    updateMainMenuScreen(abs_position, 1);
    sub_menu_position = 0;
    in_sub_menu = 0;
}


/*
 * commenceUIAction: commences an action based on user input from the UI menu
 *
 * Sets the ui_mode to the appropriate mode based on menu selection.
 * This allows ui_refresh() to handle the UI function display updates.
 */
void commenceUIAction(int absolute_position, int sub_menu_position)
{
    ui_mode_t new_mode = sub_menu_modes[absolute_position][sub_menu_position];

    // Handle special case: return to menu
    if (new_mode == UI_MODE_MENU) {
        returnMenu();
        return;
    }

    // Reset UI function parameters before entering new mode
    reset_uifunc_params();

    // Set the new UI mode (ui_refresh() will handle calling the appropriate function)
    change_ui_mode(new_mode);
}


////////////////////////////////////////////////////////////////////////
////////  HELPER LOGIC FUNCTIONS    ////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// TODO: convert these to local functions only? Does that help compiler space and stuff?

/*
 * changeMainMenuPosition: changes the absolute and relative position based on action
 */
void changeMainMenuPosition(int action)
{
    prev_pos = abs_position;

    // if we are moving up
    if (action == -1) {
        if (abs_position == 0) {  // if we are already at the top (first) absolute position
            return;
        }
        else {  // otherwise decrease position by one
            abs_position--;
        }
    }

    // if we are moving down (towards the end of the positions)
    if (action == 1) {
        if (abs_position == UI_MAIN_MENU_ITEMS-1) {  // if we are already at the end (last) absolute position
            return;
        }
        else {
            abs_position++;
        }
    }
}


/*
 * changeSubMenuPosition: changes the sub menu position based on action
 */
void changeSubMenuPosition(int action)
{
    prev_pos = sub_menu_position;

    if (action == -1) {  // if we are moving up
        if (sub_menu_position == 0) { // if already at the top do nothing
            return;
        }
        else {
            sub_menu_position--;
        }
    }

    if (action == 1) {  // if we are moving down
        if (sub_menu_position == getSubMenuLength(abs_position)-1) {  // do nothing if we would move past the menu length
            return;
        }
        else {
            sub_menu_position++;
        }
    }
}


bool get_running_state() {
    return run_sub_menu;
}


/*
 * getSubMenuLength: gets the length of a certain sub menu
 */
int getSubMenuLength(int menu_number)
{
    if (menu_number == 0) {
        return sizeof(sub_menu_0)/sizeof(sub_menu_0[0]);
    }
    else if (menu_number == 1) {
        return sizeof(sub_menu_1)/sizeof(sub_menu_0[0]);
    }

    return -1;
}











