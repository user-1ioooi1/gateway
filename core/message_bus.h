#ifndef MESSAGE_BUS_H
#define MESSAGE_BUS_H


#include "plugin.h"

int  message_bus_init(void);


// 发布消息（线程安全，插件调用此函数）
int message_bus_publish(gateway_msg_t *msg,void *userdata);

// 订阅消息
// topic_filter: 支持通配符，如 "device/+/temp" 或 "device/#"
// cb: 收到匹配消息时的回调
// userdata: 透传给回调的用户数据
int  message_bus_subscribe(const char *topic_filter,
    msg_recv_cb_t cb,
    void *userdata);

int message_bus_unsubscribe(const char *topic_filter, msg_recv_cb_t cb);

void message_bus_stop(void);






#endif