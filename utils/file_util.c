// file_utils.c
#include "file_util.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

time_t file_get_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

int file_calc_md5(const char *path, char *md5_out, int len)
{
    if (len < 33) return -1;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "md5sum %s 2>/dev/null", path);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char result[256] = {0};
    fgets(result, sizeof(result), fp);
    pclose(fp);

    if (strlen(result) < 32) return -1;
    strncpy(md5_out, result, 32);
    md5_out[32] = '\0';
    return 0;
}