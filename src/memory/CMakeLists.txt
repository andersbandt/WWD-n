target_sources(app PRIVATE
    mt29f_nand.c
    nvs.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})