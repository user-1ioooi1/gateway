add_library(processor_plugin SHARED
    processor_plugin.c
)

target_link_libraries(processor_plugin
    utils
)