#include "plugin_manager.h"
#include "logger.h"
#include <dlfcn.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "file_util.h"

#define MAX_PLUGINS 16

// 插件槽：管理一个插件的完整生命周期
typedef struct {
    void         *dl_handle;    // dlopen 返回的句柄
    plugin_ops_t *ops;          // 插件操作集
    char          so_path[256]; // .so 路径，方便热更新
    time_t        so_mtime;
    bool used;     
} plugin_slot_t;


static plugin_slot_t g_plugins[MAX_PLUGINS];
static int g_count;
static msg_recv_cb_t g_bus_publish = NULL; 
static subscribe_fn_t g_bus_subscribe = NULL;
static unsubscribe_fn_t g_bus_unsubscribe = NULL;


static plugin_slot_t *get_unused_slot(void){
    for(int i = 0; i < MAX_PLUGINS; i++){
        if(!g_plugins[i].used){
            g_plugins[i].used = true;
            g_count++;
            return &g_plugins[i];
        }
    }
    LOG_ERR("[plugin_manager] not plugin slot");
    return NULL;
}

static int get_plugin_index(const char *plugin_name){
    for(int i = 0; i < MAX_PLUGINS; i++){
        if(g_plugins[i].used && g_plugins[i].ops && strcmp(g_plugins[i].ops->get_name(), plugin_name) == 0){
            return i;
        }
    }
    //LOG_DBG("[plugin_manager] not plugin: %s",plugin_name);
    return -1;
}

static int load_plugin_in_slot(plugin_slot_t *slot, const char *so_path,const char *json_cfg){
    if (g_count >= MAX_PLUGINS) {
        LOG_ERR("Plugin slots full, max=%d", MAX_PLUGINS);
        return -1;
    }

    if(so_path == NULL || json_cfg == NULL) return -1;

    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL); //RTLD_LOCAL不加入全局符号表
    if (!handle) {
        LOG_ERR("dlopen [%s] failed: %s", so_path, dlerror());//只有手动调用 dl 系列函数才会影响 dlerror：
        return -1;
    }

    plugin_entry_fn entry = (plugin_entry_fn)dlsym(handle,
        PLUGIN_ENTRY_SYMBOL);

    char *err = dlerror(); // PLUGIN_ENTRY_SYMBOL 本身的值可能是NULL
    if(err){
        LOG_ERR("dlsym [%s] failed: %s", PLUGIN_ENTRY_SYMBOL,err);
        dlclose(handle);
        return -1;
    }

    // 获取操作集
    plugin_ops_t *ops = entry();
    if (!ops) {
        LOG_ERR("plugin_entry returned NULL");
        dlclose(handle);
        return -1;
    }
    
    if (ops->init(json_cfg,g_bus_publish,g_bus_subscribe,g_bus_unsubscribe) != 0) {
        LOG_ERR("Plugin [%s] init failed", ops->get_name());
        dlclose(handle);
        return -1;
    }
    plugin_slot_t *s = slot ? slot : get_unused_slot();
    s->dl_handle = handle;
    s->ops       = ops;
    strncpy(s->so_path, so_path, sizeof(s->so_path) - 1);
    s->used = true;
    s->so_mtime = file_get_mtime(so_path);

    LOG_INF("Plugin [%s v%s] loaded from %s",
        s->ops->get_name(), s->ops->get_version(), so_path);

    return 0;
}



int plugin_manager_init(void)
{
    memset(g_plugins, 0, sizeof(g_plugins));
    g_count = 0;
    LOG_INF("Plugin manager initialized");
    return 0;
}


void plugin_manager_set_bus_cb(msg_recv_cb_t publish_fn, subscribe_fn_t subscribe_fn, unsubscribe_fn_t unsubscribe_fn)
{
    g_bus_publish = publish_fn;
    g_bus_subscribe = subscribe_fn;
    g_bus_unsubscribe = unsubscribe_fn;

}

int plugin_manager_start_one(const char *name)
{
    int index = get_plugin_index(name);
    if(index == -1){
        return - 1;
    }
    plugin_ops_t *ops = g_plugins[index].ops;
    ops->start();
    LOG_INF("Plugin [%s] start", ops->get_name());
    return 0;
}


int plugin_manager_start_all(void)
{
    for (int i = 0; i < MAX_PLUGINS; i++) {
        if(!g_plugins[i].used) continue;
        plugin_ops_t *ops = g_plugins[i].ops;
        if (ops->start() != 0) {
            LOG_ERR("Plugin [%s] start failed", ops->get_name());
            return -1;
        }
        LOG_INF("Plugin [%s] started", ops->get_name());
    }
    return 0;
}

void plugin_manager_stop_one(const char *name){
    int index = get_plugin_index(name);
    if(index == -1){
        return;
    }
    plugin_ops_t *ops = g_plugins[index].ops;
    ops->stop();
    LOG_INF("Plugin [%s] stop", ops->get_name());
}

void plugin_manager_stop_all(void){
    for (int i = MAX_PLUGINS - 1; i >= 0; i--){ //逆序停止 避免依赖问题
        if(!g_plugins[i].used) continue;
        plugin_ops_t *ops = g_plugins[i].ops;
        ops->stop();
        LOG_INF("Plugin [%s] stop", ops->get_name());
    }
}

void plugin_manager_destroy_one(const char *name)
{
    int index = get_plugin_index(name);
    if(index == -1){
        return;
    } 
    g_plugins[index].ops->stop();
    g_plugins[index].ops->destroy();
    LOG_INF("Plugin [%s] unloaded", g_plugins[index].ops->get_name());
    dlclose(g_plugins[index].dl_handle);  // 卸载 .so
    dlerror();
    g_plugins[index].used = false;
    memset(&g_plugins[index], 0 , sizeof(plugin_slot_t));
    g_count--;
}

void plugin_manager_destroy(void)
{
    for (int i = MAX_PLUGINS - 1; i >= 0; i--) {
        if(!g_plugins[i].used) continue;
        g_plugins[i].ops->stop();
        g_plugins[i].ops->destroy();
        LOG_INF("Plugin [%s] unloaded",  g_plugins[i].ops->get_name());
        dlclose(g_plugins[i].dl_handle);  // 卸载 .so
        dlerror();
        g_plugins[i].used = false;
        memset(&g_plugins[i], 0 , sizeof(plugin_slot_t));
    } 
    g_count = 0;
}

int plugin_manager_send(const char *name, const gateway_msg_t *msg) //不同的模块处理不同消息
{

    int index = get_plugin_index(name);
    if(index == -1){
        return index;
    }
    return g_plugins[index].ops->send(msg);
}

plugin_ops_t *plugin_manager_get_ops(const char *name)
{

    int index = get_plugin_index(name);
    if(index == -1){
        return NULL;
    }
    if(g_plugins[index].used == false){
        LOG_ERR("[plugin_manager] get ops error");
        return NULL;
    }

    return g_plugins[index].ops;
}

int plugin_manager_load(const char *plugin_name,const char *so_path,const char *json_cfg){
    plugin_slot_t *p = NULL;
    int index = get_plugin_index(plugin_name);
    if(index == -1){
        LOG_DBG("[plugin_manger] new plugin : %s", plugin_name);
        return load_plugin_in_slot(p,so_path,json_cfg);
    }
    p = &g_plugins[index];
    LOG_DBG("[plugin_manger] update plugin cfg : %s",plugin_name );
    plugin_manager_destroy_one(p->ops->get_name());
    return load_plugin_in_slot(p,so_path,json_cfg);
}

int plugin_manager_change_version(const char *plugin_name,const char *so_path, time_t so_mtime, const char *json_cfg){
    plugin_slot_t *p = NULL;
    int index = get_plugin_index(plugin_name);
    p = &g_plugins[index];
    if(so_mtime == p->so_mtime){
        return 1;
    }
    LOG_DBG("[plugin_manger] update plugin version : %s",plugin_name);
    plugin_manager_destroy_one(p->ops->get_name());
    return load_plugin_in_slot(p,so_path,json_cfg);
}