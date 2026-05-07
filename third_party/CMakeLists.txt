add_library(cjson SHARED
    cJSON.c
)

target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}) #让 cjson 这个目标能够找到它自己的头文件
