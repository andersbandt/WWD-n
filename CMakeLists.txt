cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(WWDn)

# Power-profiling harness (src/power_profile/power_profile_main.c) in place
# of the real app's src/main.c — see debug/power_profile/README.md.
# Build with: cmake ... -DPOWER_PROFILE_BUILD=ON
option(POWER_PROFILE_BUILD "Build the power-profiling harness instead of the real app" OFF)

# Include all headers under src
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Collect all .c files recursively
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

if(POWER_PROFILE_BUILD)
    target_sources(app PRIVATE src/power_profile/power_profile_main.c)
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
    src/hardware/led.c
    src/hardware/button.c
    src/power/power.c
    src/util/cdc_debug.c
)

add_subdirectory(src/display)
add_subdirectory(src/memory)
add_subdirectory(src/ui)
add_subdirectory(src/hardware/ic/imu)
add_subdirectory(src/ble)
add_subdirectory(src/comm)
