cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(WWDn)

# Display driver selection
# Uncomment the line below to use ST7735S instead of ST7789
# add_compile_definitions(USE_ST7735S)

# Include all headers under src
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Collect all .c files recursively
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

# Compile all of them
target_sources(app PRIVATE 
    src/main.c
    src/circular_buffer.c
    src/clock.c
    src/peripheral/interrupt.c
    src/hardware/led.c
    # src/display/display.c
    # src/display/st7789.c
)

add_subdirectory(src/display)
add_subdirectory(src/memory)
add_subdirectory(src/hardware/ic/imu)
