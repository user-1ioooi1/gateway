add_library(utils SHARED
    logger.c
    config.c
    message.c
    file_util.c
) 

target_include_directories(utils PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/third_party        
)
target_link_libraries(utils PUBLIC cjson)
