#include "logger.h"
#include <stdarg.h>
#include <string.h>

#define COLOR_RESET  "\033[0m" //重置颜色
#define COLOR_DEBUG  "\033[36m"
#define COLOR_INFO   "\033[32m"
#define COLOR_WARN   "\033[33m"
#define COLOR_ERROR  "\033[31m"


static log_level_t g_level = LOG_DEBUG;


static const char *level_str[] = {"DBG","INF","WRN","ERR"};
static const char *level_color[] = {COLOR_DEBUG,COLOR_INFO,COLOR_WARN,COLOR_ERROR};

void log_set_level(log_level_t level){
    g_level = level;
}


void log_print(log_level_t level, const char *file, int line, const char *fmt, ...){
    if(level < g_level) return; //只报比环境更大的错
    time_t now = time(NULL); //接收返回值或者传入指针
    struct tm *t = localtime(&now);
    char time_buf[20];
    strftime(time_buf,sizeof(time_buf),"%H:%M:%S",t);
    const char *fname = strrchr(file, '/');  //右向左找字符
    fname = fname ? fname + 1 : file;

    fprintf(stderr, "%s[%s][%s][%s:%d] ",
            level_color[level],time_buf,level_str[level],fname,line); //stderr: file*
    
    va_list args;
    va_start(args,fmt);
    vfprintf(stderr,fmt,args);
    va_end(args);
    fprintf(stderr, "%s\n", COLOR_RESET);
}