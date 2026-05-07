add_executable(gateway
    main.c
    plugin_manager.c
    message_bus.c 
    signal_handler.c
    bus_stress_test.c
)

target_link_libraries(gateway
    utils 
    dl    
    pthread
    cjson
)