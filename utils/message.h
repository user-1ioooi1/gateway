#ifndef MESSAGE_H
#define MESSAGE_H


#include <stdint.h>

// 消息类型
typedef enum {
    MSG_TYPE_TELEMETRY = 0,  // 遥测：传感器上报的数值
    MSG_TYPE_COMMAND,        // 指令：云端下发的控制命令
    MSG_TYPE_EVENT,          // 事件：告警、状态变化
} msg_type_t;

typedef struct  {
    //---元数据---
    char topic[64]; //消息主题，如 "device/modbus/temp"
    msg_type_t type; 
    uint64_t timestamp_ms; //时间戳
    uint32_t seq;  //序列号，每条消息递增
    
    union {
        struct {
            char   key[32];     // 数据键名，如 "temperature"
            double value;       // 数值
            char   unit[16];    // 单位，如 "℃"
        } telemetry;

        struct {
            char cmd[32];       // 指令名，如 "set_valve"
            char params[256];   // 参数，JSON 字符串
        } command;

        // 事件
        struct {
            char name[32];      // 事件名，如 "temp_over_limit"
            char detail[256];   // 详情
            int  severity;      // 严重程度 0=info 1=warn 2=error
        } event;

    } payload;
} gateway_msg_t;

uint64_t msg_timestamp_ms(void); //获取时间戳

void msg_show(const gateway_msg_t *msg); 

#endif