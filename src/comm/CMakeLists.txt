target_sources(app PRIVATE
    protocol.c
    rate_config.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
