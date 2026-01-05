

# zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../hardware/ic/imu)


target_sources(app PRIVATE
    ui_display.c
    ui_menu.c
    ui.c
    UIFunctions.c
)


target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
