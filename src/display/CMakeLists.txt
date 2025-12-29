target_sources(app PRIVATE
    display.c
    st7789.c
    font.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})