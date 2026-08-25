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
#include <display.h>
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
#define SUB_MENU_0_LENGTH     5
char sub_menu_0[SUB_MENU_0_LENGTH][SUB_MENU_CHAR_LENGTH] = {
                         "Change Date/Time",
                         "Brightness",
                         "Low Power",
                         "Clear faults",
                         "Return"
                     };


// menu 1 sub-menu options: IMU
#define SUB_MENU_1_LENGTH     5
char sub_menu_1[SUB_MENU_1_LENGTH][SUB_MENU_CHAR_LENGTH] = {
    "Display readings",
    "Temperature",
    "Pedometer",
    "Temp Graph",
    "Return"
                     };


// menu 2 sub-menu options: Data
#define SUB_MENU_2_LENGTH     2
char sub_menu_2[SUB_MENU_2_LENGTH][SUB_MENU_CHAR_LENGTH] = {
    "Log Stats",
    "Return"
                     };


// menu 3 sub-menu options: Timer
#define SUB_MENU_3_LENGTH     2
char sub_menu_3[SUB_MENU_3_LENGTH][SUB_MENU_CHAR_LENGTH] = {
    "Stopwatch",
    "Return"
                     };


// creating array of sub menu text items
char * sub_menu[UI_MAIN_MENU_ITEMS] = {*sub_menu_0, *sub_menu_1, *sub_menu_2, *sub_menu_3};


// Sub-menu UI mode mappings
// Maps menu positions to ui_mode_t values
// UI_MODE_MENU is used for "Return" options
ui_mode_t sub_menu_modes[UI_MAIN_MENU_ITEMS][SUB_MENU_MAX_LENGTH] = {
    // System settings (Menu 0)
    {
        UI_MODE_PROMPT_TIME,        // Change Date/Time
        UI_MODE_ADJUST_BRIGHTNESS,  // Adjust Brightness
        UI_MODE_LOW_POWER,          // Low Power
        UI_MODE_CLEAR_FAULTS,       // Clear faults
        UI_MODE_MENU                // Return (back to menu)
    },

    // IMU (Menu 1)
    {
        UI_MODE_IMU_READ,           // Display readings
        UI_MODE_IMU_TEMP,           // Temperature
        UI_MODE_IMU_PEDOMETER,      // Pedometer
        UI_MODE_TEMP_GRAPH,         // Temp Graph
        UI_MODE_MENU                // Return
    },

    // Data (Menu 2)
    {
        UI_MODE_DATA_STATS,         // Log Stats
        UI_MODE_MENU                // Return
    },

    // Timer (Menu 3)
    {
        UI_MODE_STOPWATCH,          // Stopwatch
        UI_MODE_MENU                // Return
    },
};



// other local tracking variables
int prev_pos;
bool in_sub_menu = 0;
bool run_sub_menu = 0;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! LOCAL FUNCTIONS -------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// local helper prototypes
static void changeMainMenuPosition(int action);
static void changeSubMenuPosition(int action);
static int getSubMenuLength(int menu_number);
static bool should_update_page(int current_pos, int prev_position, int force);
static void render_menu_items(char **items, int start_idx, int count);
static void render_sub_menu_items(int menu_idx, int start_item_idx, int count);

/**
 * @brief changes position based on input direction
 *
 * @param action should represent the direction we are moving. +2 for "up", and +1 for "down". A 0 (for select) should not be sent to this function
 *
 * @returns None (void)
 */
static void changeMainMenuPosition(int action)
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


/**
 * @brief changeSubMenuPosition
 *
 * @param action should represent the direction we are moving. +2 for "up", and +1 for "down". A 0 (for select) should not be sent to this function
 *
 * @returns None (void)
 */
static void changeSubMenuPosition(int action)
{
    prev_pos = sub_menu_position;

    // if we are moving up
    if (action == -1) {
        if (sub_menu_position == 0) {
             // if already at the top do nothing
            return;
        }
        else {
            sub_menu_position--;
        }
    }

    // if we are moving down
    if (action == 1) {
        if (sub_menu_position == getSubMenuLength(abs_position) - 1) {
            // do nothing if we would move past the menu length
            return;
        }
        else {
            sub_menu_position++;
        }
    }
}


/**
 * @brief getSubMenuLength
 *
 * @param The menu number we are examining (0, 1, 2, ... and so on)
 *
 * @returns None (void)
 */
static int getSubMenuLength(int menu_number)
{
    if (menu_number == 0) {
        return sizeof(sub_menu_0)/sizeof(sub_menu_0[0]);
    }
    else if (menu_number == 1) {
        return sizeof(sub_menu_1)/sizeof(sub_menu_1[0]);
    }
    else if (menu_number == 2) {
        return sizeof(sub_menu_2)/sizeof(sub_menu_2[0]);
    }
    else if (menu_number == 3) {
        return sizeof(sub_menu_3)/sizeof(sub_menu_3[0]);
    }

    return -1;
}


/**
 * @brief Check if page needs updating
 *
 * @param current_pos Current menu position
 * @param prev_position Previous menu position
 * @param force Force update flag
 *
 * @returns true if page should be redrawn
 */
static bool should_update_page(int current_pos, int prev_position, int force)
{
    return (current_pos / UI_MENU_ITEMS_PAGE != prev_position / UI_MENU_ITEMS_PAGE) || force;
}


/**
 * @brief Render menu items on display
 *
 * @param items Array of string pointers to menu items
 * @param start_idx Starting index in the items array
 * @param count Number of items to render
 */
static void render_menu_items(char **items, int start_idx, int count)
{
    for (int i = 0; i < UI_MENU_ITEMS_PAGE; i++) {
        clearAndPrintLine(i < count ? items[start_idx + i] : "", i + 1, START_X, FONT_MEDIUM);
    }
}


/**
 * @brief Render sub menu items on display
 *
 * @param menu_idx Menu number (0, 1, 2, etc.)
 * @param start_item_idx Starting item index within the sub menu
 * @param count Number of items to render
 */
static void render_sub_menu_items(int menu_idx, int start_item_idx, int count)
{
    for (int i = 0; i < UI_MENU_ITEMS_PAGE; i++) {
        clearAndPrintLine(i < count ? sub_menu[menu_idx] + SUB_MENU_CHAR_LENGTH * (start_item_idx + i) : "",
                         i + 1, START_X, FONT_MEDIUM);
    }
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! GLOBAL FUNCTIONS ------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void initMenu()
{
    abs_position = 0;
    sub_menu_position = 0;

    // TODO: I think I need to incorporate this into my testing framework too?
    // in_sub_menu = true;
}


/**
 * @brief returns if we are currently running a sub menu item
 *
 * @param None
 *
 * @returns bool representing state
 */
bool get_running_state()
{
    return run_sub_menu;
}


/**
 * @brief returns whether we are currently browsing a sub-menu's item list
 * (as opposed to the top-level main menu list)
 *
 * @param None
 *
 * @returns bool representing state
 */
bool get_in_sub_menu_state()
{
    return in_sub_menu;
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
    // SUB MENU UPDATES
    if (in_sub_menu) {
        // if sub menu function already running ...
        if (run_sub_menu) {
            // Check if user wants to exit the UI function
            // if (action == 2) {
            //     run_sub_menu = false;
            //     reset_uifunc_params(); // NOTE: this has to be called to wipe things like position for setting clock
            //     returnSubMenu();
            //     change_ui_mode(UI_MODE_MENU); // Return to menu mode
            //     return;
            // }
            // UI function is still running - ui_refresh() handles calling it
            // return;
        }

        // if select button was pressed
        else if (action == 2) {
            run_sub_menu = true;
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

    // MAIN MENU UPDATES
    else {
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
    if (should_update_page(absolute_pos, prev_pos, force)) {
        int page_num = absolute_pos / UI_MENU_ITEMS_PAGE;
        int start_idx = page_num * UI_MENU_ITEMS_PAGE;
        int items_to_display = UI_MAIN_MENU_ITEMS - start_idx;

        if (items_to_display > UI_MENU_ITEMS_PAGE) {
            items_to_display = UI_MENU_ITEMS_PAGE;
        }

        render_menu_items(main_menu_options, start_idx, items_to_display);
    }

    in_sub_menu = 0;
    updateCursor(prev_pos, absolute_pos, force);
}


/*
 * updateSubMenuScreen: updates the sub menu screen based on absolute position (menu number) and sub menu position
 *
 * absolute_pos and sub_menu_pos start at 0
 */
void updateSubMenuScreen(int abs_pos, int sub_pos, int force)
{
    int page_num = sub_pos / UI_MENU_ITEMS_PAGE;
    int start_idx = page_num * UI_MENU_ITEMS_PAGE;
    int sub_menu_length = getSubMenuLength(abs_pos);
    int items_to_display = sub_menu_length - start_idx;

    if (items_to_display > UI_MENU_ITEMS_PAGE) {
        items_to_display = UI_MENU_ITEMS_PAGE;
    }

    if (should_update_page(sub_pos, prev_pos, force)) {
        render_sub_menu_items(abs_pos, start_idx, items_to_display);
    }

    // Update cursor - clamp to available items
    int cursor_pos = (sub_pos % UI_MENU_ITEMS_PAGE < items_to_display)
                     ? sub_pos
                     : items_to_display - 1;
    updateCursor(prev_pos, cursor_pos, force);
}


/*
 * updateCursor: updates the cursor (">") position
 *
 * prev_position is only meaningful when it refers to a row on the SAME
 * screen the cursor is currently being drawn on (i.e. incremental up/down
 * navigation within one menu/submenu page). Every caller that switches
 * screens (entering/returning from a submenu, changing page) passes
 * force=1 - in that case prev_position may be stale left-over state from
 * a completely different screen, so instead of trusting it to find the
 * "old" cursor row, blank the whole cursor column so nothing from a prior
 * screen can be left behind.
 */
void updateCursor(int prev_position, int position, int force)
{
   // calculate positions relative to page
   int relative_position = position % UI_MENU_ITEMS_PAGE;  // generate the 'relative position' with modulus division

   if (force) {
       for (int row = 0; row < UI_MENU_ITEMS_PAGE; row++) {
           printLine(" ", row + 1, 0, FONT_MEDIUM);
       }
   }
   else {
       int prev_relative_position = prev_position % UI_MENU_ITEMS_PAGE;
       printLine(" ", prev_relative_position+1, 0, FONT_MEDIUM); // erase old cursor
   }

   printLineTransparent(">", relative_position+1, 0, FONT_MEDIUM); // draw new cursor
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
 * returnSubMenu: returns to a sub menu
 */
void returnSubMenu()
{
    updateSubMenuScreen(abs_position, sub_menu_position, 1);
    in_sub_menu = 1;
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


/*
 * ui_menu_force_exit: clears menu/sub-menu running state unconditionally.
 * See header for why this is needed before change_ui_mode(UI_MODE_CLOCK).
 */
void ui_menu_force_exit(void)
{
    run_sub_menu = false;
    in_sub_menu = false;
    reset_uifunc_params();
}


/*
 * ui_menu_return_to_sub_menu: exits a running sub-menu function back to the
 * sub-menu list it was launched from - one level up, as opposed to
 * ui_menu_force_exit() which clears all the way back to the main menu list.
 * Used by the BACK button from within a running leaf screen (see
 * handle_ui_input() in ui.c). Caller is still responsible for setting
 * ui_mode = UI_MODE_MENU directly - NOT via change_ui_mode(), which resets
 * to the top-level main menu (abs_position = 0) whenever its target is
 * UI_MODE_MENU, which would blow away the sub-menu redraw this function
 * just did.
 */
void ui_menu_return_to_sub_menu(void)
{
    run_sub_menu = false;
    reset_uifunc_params();
    returnSubMenu();
}














