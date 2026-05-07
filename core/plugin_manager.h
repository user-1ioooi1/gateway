#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include "plugin.h"
#include "file_util.h"

// 插件管理器初始化
int  plugin_manager_init(void);

//卸载所有插件
void plugin_manager_destroy(void);

// 加载一个插件
// so_path: .so 文件路径
// json_cfg: 传给插件 init() 的配置串
int  plugin_manager_load(const char *plugin_name,const char *so_path, const char *json_cfg);
int plugin_manager_change_version(const char *plugin_name,const char *so_path, time_t so_mtime, const char *json_cfg);
// 启动所有已加载插件
int  plugin_manager_start_all(void);
// 停止所有插件
void plugin_manager_stop_all(void);

int plugin_manager_start_one(const char *name);
void plugin_manager_stop_one(const char *name);

// 向指定插件发送消息（下行）
int  plugin_manager_send(const char *plugin_name,
    const gateway_msg_t *msg);

// 注册消息总线推送和订阅函数
void plugin_manager_set_bus_cb(msg_recv_cb_t publish_fn, subscribe_fn_t subscribe_fn , unsubscribe_fn_t unsubscribe_fn);

void plugin_manager_destroy_one(const char *name);

plugin_ops_t *plugin_manager_get_ops(const char *name);


/*0.没变化*/
/*1.软件改变->重新加载插件 */
/*2.配置改变->重新加载插件 */
/*3.新插件*/
/*4. 拔掉插件*/

//0,2,3

#endif