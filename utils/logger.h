#ifndef LOGGER_H
#define LOGGER_H


#include <stdio.h>
#include <time.h>

typedef enum {
    LOG_DEBUG = 0, //调试
    LOG_INFO, //信息
    LOG_WARN,  //警告
    LOG_ERROR //错误
} log_level_t;

void log_set_level(log_level_t level);

// ##__VA_ARGS__ __VA_ARGS__接收...；  ## 是预处理连接符，当 __VA_ARGS__ 为空时：会删除它前面的逗号，避免语法错误
#define LOG_DBG(fmt, ...) log_print(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__) 
#define LOG_INF(fmt, ...) log_print(LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) log_print(LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) log_print(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

void log_print(log_level_t level, const char *file, int line, const char *fmt, ...);
/*__FILE__ → 编译器自动替换成当前文件名字符串，包含路径如 "util/main.c"
__LINE__ → 自动替换成当前行号整数，如 42
fmt -> 格式字符串
*/


#endif