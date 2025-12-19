# src/hardware/ic/imu/CMakeLists.txt

target_sources(app PRIVATE
    # imu.c
    # ICM_42670.c
    # inv_imu_driver.c
    # inv_imu_apex.c
    # imu_process.c
    # inv_imu_transport.c
    # inv_time.c
    icm42670.c
)


target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # imu folder
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..  # top-level src folder
)
