add_library(mqtt_plugin SHARED
    mqtt_plugin.c
)

target_link_libraries(mqtt_plugin
    utils
    mosquitto
)