target_sources(app PRIVATE
    display.c
    st7735s/st7735s.c
    st7735s/st7735s_compat.c
    st7735s/gfx.c
    st7735s/fonts.c
    st7789.c
    font.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/st7735s
)