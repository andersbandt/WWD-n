target_sources(app PRIVATE
    imu.c
    ICM_42670.c
    inv_imu_driver.c
    inv_imu_apex.c
    imu_process.c
    inv_imu_transport.c
    inv_time.c
    # icm42670.c
)


target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    # ${CMAKE_CURRENT_SOURCE_DIR}/../../..  # top-level src folder
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
