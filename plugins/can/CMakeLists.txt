add_library(can_plugin SHARED
    can_plugin.c
)

target_link_libraries(can_plugin
    utils
)