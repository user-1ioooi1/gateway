add_library(modbus_plugin SHARED
    modbus_plugin.c
)

target_link_libraries(modbus_plugin
    utils
    modbus  
)

target_include_directories(modbus_plugin PRIVATE
    ${PROJECT_SOURCE_DIR}/plugins/modbus
)