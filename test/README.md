# UI Functions Test Harness

## Overview
`test_uifunctions.c` is a standalone test file that initializes all necessary subsystems and allows you to test individual UI functions in isolation.

## Features
- ✅ Initializes all required hardware (display, buttons, LEDs)
- ✅ Sets up interrupts and timers
- ✅ Provides logging output for debugging
- ✅ Easy to switch between different function tests
- ✅ Follows same initialization sequence as main.c

## How to Use

### Method 1: Replace main.c temporarily

1. **Backup your main.c:**
   ```bash
   cp src/main.c src/main.c.backup
   ```

2. **Copy test file to main.c:**
   ```bash
   cp src/ui/test_uifunctions.c src/main.c
   ```

3. **Select which function to test:**
   Edit the test defines in the file (around line 57):
   ```c
   /* Select which function to test - uncomment ONE */
   #define TEST_PROMPT_FOR_TIME
   // #define TEST_CHANGE_CONTRAST
   // #define TEST_CLEAR_FAULTS
   // #define TEST_IMU_READ
   // #define TEST_IMU_TEMP
   ```

4. **Build and flash:**
   ```bash
   west build -b your_board_name
   west flash
   ```

5. **Restore main.c when done:**
   ```bash
   cp src/main.c.backup src/main.c
   ```

### Method 2: Add as separate build target (Recommended)

Modify your `CMakeLists.txt` to conditionally build the test file:

```cmake
# Add this option at the top
option(BUILD_UI_TESTS "Build UI function tests" OFF)

# Then modify the app source selection
if(BUILD_UI_TESTS)
    target_sources(app PRIVATE src/ui/test_uifunctions.c)
else()
    target_sources(app PRIVATE src/main.c)
endif()
```

Build with tests:
```bash
west build -b your_board_name -- -DBUILD_UI_TESTS=ON
west flash
```

Build normally:
```bash
west build -b your_board_name
west flash
```

## Available Test Functions

### 1. TEST_PROMPT_FOR_TIME
Tests the time prompt UI function.
- **Button 1:** Increment current field (hours/minutes/seconds)
- **Button 3/4:** Advance to next field
- Cycles through: HOURS → MINUTES → SECONDS → Done

### 2. TEST_CHANGE_CONTRAST
Tests the display contrast adjustment.
- **Button 1:** Decrease contrast
- **Button 2:** Increase contrast
- **Both buttons:** Exit

### 3. TEST_CLEAR_FAULTS
Tests the fault clearing function (currently minimal implementation).

### 4. TEST_IMU_READ
Tests reading and displaying IMU accelerometer data.
- Requires IMU to be initialized (may need to uncomment IMU init code)
- Displays 5 consecutive readings with 1 second intervals

### 5. TEST_IMU_TEMP
Tests reading and displaying IMU temperature.
- Requires IMU to be initialized
- Displays temperature for 3 seconds

## Quick Function Testing (No Rebuild)

If you want to test functions **without** modifying files or rebuilding, you can add this to your existing `main.c`:

```c
/* Add after init_display() and init_ui() */

#ifdef UI_FUNCTION_TEST
    k_msleep(2000);  /* Delay before test */
    clear_display();

    /* Call any UI function directly */
    system_change_display_contrast_UI_FUNC();
    // or
    // imuRead_UI_FUNC();
    // or
    // system_prompt_for_time_UI_FUNC();

    LOG_INF("Test complete");
#endif
```

Then build with:
```bash
west build -b your_board_name -- -DUI_FUNCTION_TEST=ON
```

## Initialization Sequence

The test harness initializes components in this order:
1. LED (for visual feedback)
2. Buttons
3. Display
4. UI
5. Interrupts
6. Timers
7. Reset UI function parameters

This matches the initialization order in your main application.

## Troubleshooting

**Problem:** Display not showing anything
- Check `display_status` in logs
- Verify display connections
- Try increasing initialization delay

**Problem:** Buttons not responding
- Check interrupt configuration
- Verify button semaphores are working
- Check button GPIO configuration

**Problem:** IMU tests failing
- Uncomment IMU initialization code in main
- Check IMU I2C/SPI connections
- Verify IMU is powered

**Problem:** Memory errors
- Increase stack sizes if needed (check LOG output)
- The test harness uses same stack as main app

## Extending the Tests

To add a new test function:

1. Add extern declaration:
   ```c
   extern void your_new_function_UI_FUNC(void);
   ```

2. Add test define:
   ```c
   // #define TEST_YOUR_NEW_FUNCTION
   ```

3. Create test function:
   ```c
   static void test_your_new_function(void)
   {
       LOG_INF("Testing: your_new_function_UI_FUNC()");
       clear_display();
       your_new_function_UI_FUNC();
       LOG_INF("Test completed");
   }
   ```

4. Call in main:
   ```c
   #ifdef TEST_YOUR_NEW_FUNCTION
       test_your_new_function();
   #endif
   ```

## Notes

- Only one test define should be uncommented at a time
- Some functions may require additional hardware (IMU, BMS)
- Logging output goes to serial console
- System idles after test completion (safe to disconnect)
