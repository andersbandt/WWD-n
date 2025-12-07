cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(WWDn)

# Include all headers under src
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Collect all .c files recursively
file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

# Compile all of them
target_sources(app PRIVATE 
    src/main.c
    #src/peripheral/interrupt.c
    src/hardware/led.c
    src/memory/mt29f_nand.c
)

