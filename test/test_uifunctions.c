//*****************************************************************************
//!
//! @file test_uifunctions.c
//! @author Test harness for UIFunctions
//! @brief Standalone test file to initialize system and test UI functions
//! @version 1.1
//! @date July 2026
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! HEADER FILES
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* Zephyr files */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

/* Hardware */
#include <hardware/led.h>
#include <hardware/button.h>
#include <display.h>
#include <ui.h>
#include <imu.h>
#include <peripheral/clock.h>

/* Peripheral */
#include <peripheral/interrupt.h>
#include <peripheral/timer.h>

LOG_MODULE_REGISTER(ui_test, LOG_LEVEL_INF);

/* CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=n (see prj.conf) - USB is brought up
 * explicitly, same as main.c, or there is no ttyACM console at all. */
static const struct device *const cdc_dev =
    DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! EXTERNAL FUNCTION DECLARATIONS FROM UIFunctions.c
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* System Settings Functions (Menu 0) */
extern void system_prompt_for_time_UI_FUNC(void);
extern void system_change_display_contrast_UI_FUNC(void);
extern void system_clear_faults_UI_FUNC(void);

/* IMU Functions (Menu 1) */
extern void imuRead_UI_FUNC(void);
extern void imutempRead_UI_FUNC(void);
extern void pedometer_UI_FUNC(void);

/* Data Functions (Menu 2) */
extern void data_stats_UI_FUNC(void);

/* Timer Functions (Menu 3) */
extern void stopwatch_UI_FUNC(void);

/* Helper function */
extern void reset_uifunc_params(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! TEST CONFIGURATION
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Select which function to test - uncomment ONE, reflash to switch */
// #define TEST_PROMPT_FOR_TIME
// #define TEST_CHANGE_CONTRAST
// #define TEST_CLEAR_FAULTS
// #define TEST_IMU_READ
// #define TEST_IMU_TEMP
// #define TEST_PEDOMETER
// #define TEST_DATA_STATS
#define TEST_STOPWATCH

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! INITIALIZATION FUNCTION
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initialize all subsystems needed for UI function testing
 *
 * This follows the same initialization sequence as main.c but simplified
 * for testing purposes.
 */
static void init_test_environment(void)
{
    int ret;

    if (!device_is_ready(cdc_dev)) {
        /* No console yet - nothing to log this to */
    }

    ret = usb_enable(NULL);
    (void)ret; /* nothing to log yet either way, console isn't up until this returns */

    LOG_INF("=== UI Function Test Harness ===");
    LOG_INF("Initializing test environment...");

    /* Initialize LED for visual feedback */
    led_init();
    led_fast_blink(1, 5);

    /* Initialize buttons */
    init_buttons();
    LOG_INF("Buttons initialized");

    /* Initialize display */
    led_set(1, 1);
    init_display();
    led_set(1, 0);
    LOG_INF("Display initialized (status: %d)", display_status);

    /* Initialize UI */
    init_ui();
    if (display_status == 1) {
        ui_refresh();
    }
    LOG_INF("UI initialized");

    /* Initialize interrupts for button handling */
    config_all_interrupts();
    LOG_INF("Interrupts configured");

    /* Initialize timers */
    init_timer();
    LOG_INF("Timers initialized");

    /* Reset UI function parameters */
    reset_uifunc_params();

    LOG_INF("Test environment ready!");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! TEST EXECUTION FUNCTIONS
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Test the prompt for time UI function
 */
static void test_prompt_for_time(void)
{
    LOG_INF("Testing: system_prompt_for_time_UI_FUNC()");
    LOG_INF("Instructions:");
    LOG_INF("  - Use button 1 to increment");
    LOG_INF("  - Use button 3/4 to advance to next field");
    LOG_INF("  - Will cycle through HOURS -> MINUTES -> SECONDS");

    k_msleep(2000); /* Give user time to read instructions */

    clear_display();
    system_prompt_for_time_UI_FUNC();

    LOG_INF("Test completed: system_prompt_for_time_UI_FUNC()");
}

/**
 * @brief Test the display contrast adjustment UI function
 */
static void test_change_contrast(void)
{
    LOG_INF("Testing: system_change_display_contrast_UI_FUNC()");
    LOG_INF("Instructions:");
    LOG_INF("  - Button 1: Decrease contrast");
    LOG_INF("  - Button 2: Increase contrast");
    LOG_INF("  - Both buttons: Exit");

    k_msleep(2000);

    clear_display();
    system_change_display_contrast_UI_FUNC();

    LOG_INF("Test completed: system_change_display_contrast_UI_FUNC()");
}

/**
 * @brief Test the clear faults UI function
 */
static void test_clear_faults(void)
{
    LOG_INF("Testing: system_clear_faults_UI_FUNC()");

    system_clear_faults_UI_FUNC();

    LOG_INF("Test completed: system_clear_faults_UI_FUNC()");
}

/**
 * @brief Test the IMU read UI function
 */
static void test_imu_read(void)
{
    LOG_INF("Testing: imuRead_UI_FUNC()");
    LOG_INF("Note: Requires IMU to be initialized");

    clear_display();

    /* Call the function multiple times to show different readings */
    for (int i = 0; i < 5; i++) {
        imuRead_UI_FUNC();
        k_msleep(1000);
    }

    LOG_INF("Test completed: imuRead_UI_FUNC()");
}

/**
 * @brief Test the IMU temperature read UI function
 */
static void test_imu_temp(void)
{
    LOG_INF("Testing: imutempRead_UI_FUNC()");
    LOG_INF("Note: Requires IMU to be initialized");

    clear_display();
    imutempRead_UI_FUNC();

    k_msleep(3000); /* Keep display visible */

    LOG_INF("Test completed: imutempRead_UI_FUNC()");
}

/**
 * @brief Test the pedometer UI function
 */
static void test_pedometer(void)
{
    LOG_INF("Testing: pedometer_UI_FUNC()");
    LOG_INF("Note: step_count comes from the IMU driver - will read 0 unless");
    LOG_INF("the IMU/APEX pedometer has been initialized and steps taken");

    clear_display();

    for (int i = 0; i < 5; i++) {
        pedometer_UI_FUNC();
        k_msleep(1000);
    }

    LOG_INF("Test completed: pedometer_UI_FUNC()");
}

/**
 * @brief Test the NVS log stats UI function
 */
static void test_data_stats(void)
{
    LOG_INF("Testing: data_stats_UI_FUNC()");
    LOG_INF("Note: this minimal harness does not call nvs_init(), so");
    LOG_INF("nvs_ready() will be false here and the screen should show the");
    LOG_INF("'NVS -1' not-ready fallback - that's the guard path working");
    LOG_INF("as intended, not a failure. Run from the real app for live values.");

    clear_display();
    data_stats_UI_FUNC();

    k_msleep(3000);

    LOG_INF("Test completed: data_stats_UI_FUNC()");
}

/**
 * @brief Test the stopwatch UI function
 *
 * No button hardware exists yet (MCP23008 not populated), so real button
 * presses aren't possible. button_buffer_push() writes to the exact same
 * circular buffer get_button_event() reads from inside stopwatch_UI_FUNC(),
 * so this synthesizes the button presses a user would otherwise make -
 * exercising start/pause/resume/reset end-to-end over UART + the display
 * without needing any button input.
 */
static void test_stopwatch(void)
{
    LOG_INF("Testing: stopwatch_UI_FUNC()");
    LOG_INF("Synthesizing button presses (no button hardware yet) - watch");
    LOG_INF("the display and these log lines together");

    clear_display();

    LOG_INF("-> initial state (should be 00:00 PAUSED)");
    stopwatch_UI_FUNC();
    k_msleep(1500);

    LOG_INF("-> injecting BUTTON_1 (start)");
    button_buffer_push(BUTTON_1_MASK);
    stopwatch_UI_FUNC();

    LOG_INF("-> letting it run for 5s (should count up, RUNNING)");
    for (int i = 0; i < 5; i++) {
        k_msleep(1000);
        stopwatch_UI_FUNC();
    }

    LOG_INF("-> injecting BUTTON_1 (pause, should freeze around 00:05)");
    button_buffer_push(BUTTON_1_MASK);
    stopwatch_UI_FUNC();

    LOG_INF("-> waiting 2s while paused (elapsed must NOT change)");
    k_msleep(2000);
    stopwatch_UI_FUNC();

    LOG_INF("-> injecting BUTTON_4 (reset, should go back to 00:00 PAUSED)");
    button_buffer_push(BUTTON_4_MASK);
    stopwatch_UI_FUNC();

    LOG_INF("Test completed: stopwatch_UI_FUNC()");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! MAIN TEST FUNCTION
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(void)
{
    /* Initialize everything needed for UI functions */
    init_test_environment();

    /* Wait a moment before starting test */
    k_msleep(1000);

    /* Run the selected test */
#ifdef TEST_PROMPT_FOR_TIME
    test_prompt_for_time();
#endif

#ifdef TEST_CHANGE_CONTRAST
    test_change_contrast();
#endif

#ifdef TEST_CLEAR_FAULTS
    test_clear_faults();
#endif

#ifdef TEST_IMU_READ
    test_imu_read();
#endif

#ifdef TEST_IMU_TEMP
    test_imu_temp();
#endif

#ifdef TEST_PEDOMETER
    test_pedometer();
#endif

#ifdef TEST_DATA_STATS
    test_data_stats();
#endif

#ifdef TEST_STOPWATCH
    test_stopwatch();
#endif

    LOG_INF("=== All tests completed ===");
    LOG_INF("System will now idle. You can modify #defines to test other functions.");

    /* Keep system running */
    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
