cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(WWDn)

# Power-profiling harness (src/power_profile/power_profile_main.c) in place
# of the real app's src/main.c — see debug/power_profile/README.md.
# Build with: cmake ... -DPOWER_PROFILE_BUILD=ON
option(POWER_PROFILE_BUILD "Build the power-profiling harness instead of the real app" OFF)

# UI function test harness (test/test_uifunctions.c) in place of the real
# app's src/main.c — see test/README.md. Build with: cmake ... -DBUILD_UI_TESTS=ON
option(BUILD_UI_TESTS "Build the UI function test harness instead of the real app" OFF)

# VBAT-sweep hold mode for the power-profiling harness. Set to 0 or 1 to build
# an image that skips the scripted sequence and instead parks the board in one
# quiet state with BOOST_SEL held at that level, so the host can sweep VBAT
# underneath it. Empty (default) = run the normal 21-state sequence.
# Build with: cmake ... -DPOWER_PROFILE_BUILD=ON -DPOWER_PROFILE_BOOST_HOLD=1
# Additive attribution mode: start with nothing initialised and bring one
# subsystem up per dwell, to find what owns the idle floor.
# Build with: cmake ... -DPOWER_PROFILE_BUILD=ON -DPOWER_PROFILE_TEARDOWN=ON
option(POWER_PROFILE_TEARDOWN "Build the additive teardown/attribution image" OFF)

# Probe image: isolates what owns the idle floor (bare_spin vs bare_idle) and
# what the IMU gyro costs (accel_only vs accel_plus_gyro).
# Build with: cmake ... -DPOWER_PROFILE_BUILD=ON -DPOWER_PROFILE_PROBE=ON
option(POWER_PROFILE_PROBE "Build the idle-floor / gyro probe image" OFF)

# BLE image: cost of the advertiser (scripted) plus the cost of a live
# connection and data transfer (host-driven, board holds advertising).
# Build with: cmake ... -DPOWER_PROFILE_BUILD=ON -DPOWER_PROFILE_BLE=ON
option(POWER_PROFILE_BLE "Build the BLE advertising / connection power image" OFF)

set(POWER_PROFILE_BOOST_HOLD "" CACHE STRING
    "0 or 1 to build a BOOST_SEL hold-mode image for VBAT sweeps; empty = normal sequence")

# Include all headers under src
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Collect all .c files recursively
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

if(POWER_PROFILE_BUILD)
    target_sources(app PRIVATE src/power_profile/power_profile_main.c)
    if(POWER_PROFILE_PROBE)
        target_compile_definitions(app PRIVATE PP_PROBE=1)
    endif()
    if(POWER_PROFILE_TEARDOWN)
        target_compile_definitions(app PRIVATE PP_TEARDOWN=1)
    endif()
    if(POWER_PROFILE_BLE)
        target_compile_definitions(app PRIVATE PP_BLE=1)
    endif()
    if(NOT POWER_PROFILE_BOOST_HOLD STREQUAL "")
        target_compile_definitions(app PRIVATE PP_BOOST_HOLD=${POWER_PROFILE_BOOST_HOLD})
    endif()
elseif(BUILD_UI_TESTS)
    target_sources(app PRIVATE test/test_uifunctions.c)
else()
    target_sources(app PRIVATE src/main.c)
endif()

# Compile all of them
target_sources(app PRIVATE
    src/circular_buffer.c
    src/peripheral/clock.c
    src/peripheral/interrupt.c
    src/peripheral/timer.c
    src/peripheral/rtc.c
    src/peripheral/rv3028.c
    src/peripheral/rv3028_bringup.c
    src/peripheral/soc_temp.c
    src/activity/activity.c
    src/hardware/led.c
    src/hardware/button.c
    src/power/power.c
    src/power/low_power.c
    src/util/cdc_debug.c
)

add_subdirectory(src/display)
add_subdirectory(src/memory)
add_subdirectory(src/ui)
add_subdirectory(src/hardware/ic/imu)
add_subdirectory(src/ble)
add_subdirectory(src/comm)
