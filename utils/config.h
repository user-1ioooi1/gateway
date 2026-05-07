#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#define MAX_PLUGINS     16
#define MAX_CFG_STR     128

/*加载json配置*/

typedef enum{
    Unused = 0,
    Retain,
    Delete,
    Update
} Plugin_cfg_state_t;

typedef struct{
    char type[16]; // "south" / "north" / "processor"
    char so_path[256]; // 插件 .so 文件路径
    char name[64]; // 插件名，如 "modbus"

    char json_cfg[1024]; // 原始 JSON 字符串，插件自己去解析细节

    //bool changed;
    Plugin_cfg_state_t state;
}plugin_cfg_t;

typedef struct{
    char gw_id[64]; //ID
    int log_level;
    plugin_cfg_t plugins[MAX_PLUGINS];
    int plugin_count;
} gateway_cfg_t;


void config_init(gateway_cfg_t *cfg);
int config_update(const char *jfile_path,gateway_cfg_t *cfg);
void config_show(const gateway_cfg_t *cfg);

#endif