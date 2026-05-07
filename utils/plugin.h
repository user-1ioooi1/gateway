#ifndef PLUGIN_H
#define PLUGIN_H

#include "message.h"

typedef int (*msg_recv_cb_t)(gateway_msg_t *msg, void *userdata); //消息回调

typedef int (*subscribe_fn_t)(const char *topic_filter,msg_recv_cb_t cb,void *userdata);

typedef int (*unsubscribe_fn_t)(const char *topic_filter,msg_recv_cb_t cb);

typedef struct{
    int (*init)(const char* json_cfg, const msg_recv_cb_t  publish_fn, const subscribe_fn_t subscribe_fn , const unsubscribe_fn_t unsubscribe_fn);
    int (*start)(void);
    void (*stop)(void);
    void (*destroy)(void);

    int (*send)(const gateway_msg_t *msg);//主程序调用此函数向插件发送消息
 
    const char *(*get_name)(void);

    const char *(*get_version)(void);
}plugin_ops_t;

// 每个插件 .so 必须导出的入口函数,只暴露操作集合
// dlsym 用这个名字找到插件入口
typedef plugin_ops_t *(*plugin_entry_fn)(void);

#define PLUGIN_ENTRY_SYMBOL  "plugin_entry"

#endif