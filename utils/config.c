#include "config.h"
#include "logger.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char  *path){
    FILE *f = fopen(path, "r");
    if(!f){
        LOG_ERR("Cannot open config");
        return NULL;
    }
    fseek(f,0,SEEK_END); //移动到最后
    size_t size = ftell(f);
    fseek(f,0,SEEK_SET); //开头

    char *buf = malloc(size + 1);

    if(!buf){
        fclose(f);
        return NULL;
    }

    fread(buf,sizeof(char),size,f); //按字节拷贝到buf
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static int get_plugin_cfg_index(gateway_cfg_t *cfg,char *plugin_name){
    for(int i = 0; i < MAX_PLUGINS; i++){
        if(cfg->plugins[i].state == Unused) continue;
        if(strcmp(cfg->plugins[i].name,plugin_name) == 0)
            return i;
    }
    return -1;
}

/*
static bool plugin_cfg_is_same(plugin_cfg_t *old_cfg, plugin_cfg_t *new_cfg){
    if(strcmp(old_cfg->type, new_cfg->type) != 0){
        return false;
    }else if(strcmp(old_cfg->name, new_cfg->name) != 0){
        return false;
    }else if(strcmp(old_cfg->so_path,new_cfg->so_path) != 0){
        return false;
    }else if(strcmp(old_cfg->json_cfg,new_cfg->json_cfg) != 0){
        return false;
    }
    return true;
}
*/

void config_init(gateway_cfg_t *cfg){
    memset(cfg,0,sizeof(*cfg));
}

void config_show(const gateway_cfg_t *cfg){
    LOG_INF("=== Gateway Config ===");
    LOG_INF("  ID        : %s", cfg->gw_id);
    LOG_INF("  LogLevel  : %d", cfg->log_level);
    LOG_INF("  Plugins   : %d", cfg->plugin_count);
    for (int i = 0; i < MAX_PLUGINS; i++) {
        const plugin_cfg_t *p = &cfg->plugins[i];
        
        // 添加调试：打印所有非 Unused 的状态
        if(p->state == Unused || p->state == Delete) continue;

            const char *state_str = 
                p->state == Retain ? "Retain" :
                p->state == Update ? "Update" : "Unknown";
            LOG_INF("  [%d] state=%-8s type=%-10s name=%-10s so=%s",
                    i, state_str, p->type, p->name, p->so_path);
        
    }
    LOG_INF("======================");
}




int config_update(const char *jfile_path, gateway_cfg_t *cfg){
    for(int i = 0; i < MAX_PLUGINS; i++){
        if(cfg->plugins[i].state != Unused){
            cfg->plugins[i].state = Delete;
        }
    }
    char *content = read_file(jfile_path);
    if (!content) return -1;
    cJSON *root = cJSON_Parse(content);
    free(content);  // 解析完就可以释放原始字符串

    if(!root){
        const char *err = cJSON_GetErrorPtr();
        LOG_ERR("JSON parse error near: %s", err ? err : "unknown");
        return -1;
    }

    cJSON *gw_id = cJSON_GetObjectItem(root,"gateway_id");
    if (cJSON_IsString(gw_id)) {
        strncpy(cfg->gw_id, gw_id->valuestring, sizeof(cfg->gw_id) - 1);
    }

    cJSON *log_level = cJSON_GetObjectItem(root, "log_level");
    if (cJSON_IsNumber(log_level)) {
        cfg->log_level = (int)log_level->valuedouble;
    }

    cJSON *plugins = cJSON_GetObjectItem(root, "plugins");
    if(!cJSON_IsArray(plugins)){
        LOG_ERR("'plugins' field missing or not an array");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *item = NULL;
    cfg->plugin_count = 0;
    cJSON_ArrayForEach(item, plugins){
        if(cfg->plugin_count >= MAX_PLUGINS){
            LOG_WRN("Too many plugins, max=%d", MAX_PLUGINS);
            break;
        }
        cJSON *type     = cJSON_GetObjectItem(item, "type");
        cJSON *name     = cJSON_GetObjectItem(item, "name");
        cJSON *so_path  = cJSON_GetObjectItem(item, "so_path");
        cJSON *config   = cJSON_GetObjectItem(item, "config");

  
        if (!cJSON_IsString(type) || !cJSON_IsString(so_path) || !cJSON_IsString(name)) {
            LOG_WRN("Plugin #%d missing 'type' or 'so_path' or 'name', skip",
                    cfg->plugin_count);
            continue;
        }

        if(!config){ //判断字段是否存在
            LOG_WRN("Plugin #%d missing 'config' ",
                cfg->plugin_count);
            continue; 
        }
        char *cfg_str = cJSON_PrintUnformatted(config); //对象序列化回JSON字符串
        if(!cfg_str){
            LOG_ERR("Failed to serialize 'config' for plugin #%d", cfg->plugin_count);
            continue;
        }
        int index = get_plugin_cfg_index(cfg, name->valuestring);
        plugin_cfg_t *p = NULL;
        cfg->plugin_count++;
        if(index == -1){ //新的模块
            for(int i = 0; i < MAX_PLUGINS; i++){
                if(cfg->plugins[i].state == Unused){
                    p = &cfg->plugins[i];
                    break;
                }
            }
            if(!p){  
                LOG_ERR("No free plugin slot for %s", name->valuestring);
                free(cfg_str);
                continue;
            }
        }else{
            p = &cfg->plugins[index]; 
            if(strcmp(p->type, type->valuestring) == 0 &&  strcmp(p->name, name->valuestring) == 0 &&
                strcmp(p->so_path ,so_path->valuestring) == 0 && strcmp(p->json_cfg, cfg_str) == 0){

                p->state = Retain;
                free(cfg_str);  // 手动 free
                continue;
            }
                
        }
        p->state = Update;
        strncpy(p->type,    type->valuestring,    sizeof(p->type)    - 1);
        p->type[sizeof(p->type) - 1] = '\0';
        strncpy(p->so_path, so_path->valuestring, sizeof(p->so_path) - 1);
        p->so_path[sizeof(p->so_path) - 1] = '\0';
        strncpy(p->name, name->valuestring, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';
        strncpy(p->json_cfg, cfg_str, sizeof(p->json_cfg) - 1);
        p->json_cfg[sizeof(p->json_cfg) - 1] = '\0';
        free(cfg_str);  // 手动 free
    }
    cJSON_Delete(root);  // 释放整棵 JSON 树
/*
    for(int i = 0; i < MAX_PLUGINS; i++){
        if(cfg->plugins[i].state == Delete){
            memset(&cfg->plugins[i], 0 , sizeof(plugin_cfg_t));
            cfg->plugins[i].state = Unused;
        }
    }
*/
    LOG_INF("Config loaded: gw_id=%s, plugins=%d",
        cfg->gw_id, cfg->plugin_count);

    return 0;

}