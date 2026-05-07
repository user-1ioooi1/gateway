#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <time.h>

// 获取文件最后修改时间
time_t file_get_mtime(const char *path);

// 计算文件 MD5（返回32位十六进制字符串）
int file_calc_md5(const char *path, char *md5_out, int len);

#endif