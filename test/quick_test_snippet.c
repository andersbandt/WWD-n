//*****************************************************************************
//!
//! @file quick_test_snippet.c
//! @brief Code snippet to paste into main.c for quick UI function testing
//!
//! INSTRUCTIONS:
//! 1. Copy the code between the START and END markers below
//! 2. Paste it into main.c right after ui_refresh() call (around line 222)
//! 3. Uncomment the function you want to test
//! 4. Build and flash
//!
//*****************************************************************************

/* ============= START: PASTE THIS INTO main.c AFTER ui_refresh() ============= */

    #if 1  /* Set to 1 to enable quick testing, 0 to disable */

    /* External declarations for UI functions */
    extern void system_prompt_for_time_UI_FUNC(void);
    extern void system_change_display_contrast_UI_FUNC(void);
    extern void imuRead_UI_FUNC(void);
    extern void imutempRead_UI_FUNC(void);
    extern void reset_uifunc_params(void);

    LOG_INF("=== QUICK UI FUNCTION TEST ===");
    LOG_INF("Starting in 2 seconds...");
    k_msleep(2000);

    /* Reset UI function parameters before test */
    reset_uifunc_params();
    clear_display();

    /* UNCOMMENT THE FUNCTION YOU WANT TO TEST: */

    // system_prompt_for_time_UI_FUNC();

    system_change_display_contrast_UI_FUNC();

    // imuRead_UI_FUNC();

    // imutempRead_UI_FUNC();

    LOG_INF("=== TEST COMPLETED ===");
    clear_display();

    #endif /* Quick test enable */

/* ============= END: PASTE SECTION ============= */


/*
 * ALTERNATIVE: Even simpler inline test
 *
 * Just add this single line after ui_refresh() to test any function:
 *
 *     system_change_display_contrast_UI_FUNC();  // or any other UI_FUNC
 *
 * That's it! No need for the full snippet above if you just want to quickly
 * test one function.
 */
