option(USE_DERS_IMU "Include inv_imu_apex.c" ON)
option(USE_ZEPHYR_IMU "Use Zephyr IMU driver" OFF)


# TODO: evaluate having the above options roll into a Cmake compile list
# two options suggested by Claude
#   1. Reverse direction - CMake → C (most common)
#   Set the option in CMake and make it available in your C code:
#   if(USE_ZEPHYR_IMU)
#       target_compile_definitions(app PRIVATE USE_ZEPHYR_IMU=1)
#   endif()
#   Then in your C code:
#   #ifdef USE_ZEPHYR_IMU
#       // Use Zephyr driver
#   #else
#       // Use Der's driver
#   #endif

#   2. Configuration header file
#   Have CMake generate a config header based on options:
#   configure_file(imu_config.h.in imu_config.h)

#   3. Read from an external file
#   CMake can read a text file at configuration time and use those values.



target_sources(app PRIVATE
    imu.c
)

if (USE_DERS_IMU)
    target_sources(app PRIVATE
        ICM_42670.c
        inv_imu_driver.c
        inv_imu_apex.c
        imu_process.c
        inv_imu_transport.c
        inv_time.c
    )
endif()


if (USE_ZEPHYR_IMU)
    target_sources(app PRIVATE
        icm42670.c
    )
endif()


target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)


# ZEPHYR LIBRARY METHOD (not working)
# zephyr_library()

# zephyr_library_sources(
#     imu.c
#     ICM_42670.c
#     inv_imu_driver.c
#     inv_imu_apex.c
#     imu_process.c
#     inv_imu_transport.c
#     inv_time.c
#     # icm42670.c
# )


# zephyr_library_include_directories(
#     ${CMAKE_CURRENT_SOURCE_DIR}           # imu folder
#     ${CMAKE_CURRENT_SOURCE_DIR}/../../..  # top-level src folder
# )
