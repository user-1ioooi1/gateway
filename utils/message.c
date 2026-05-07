#include "message.h"
#include "logger.h"
#include <stdio.h>
#include <time.h>


uint64_t msg_timestamp_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); //unix测量精确到ns
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; //s与ns转换成ms
}

void msg_show(const gateway_msg_t *msg){
    const char *type_str[] = { "TELEMETRY", "COMMAND", "EVENT" };
    LOG_DBG("--- Message ---");
    LOG_DBG("  topic : %s", msg->topic);
    LOG_DBG("  type  : %s", type_str[msg->type]);
    LOG_DBG("  seq   : %u", msg->seq);
    LOG_DBG("  ts_ms : %llu", (unsigned long long)msg->timestamp_ms);

    switch(msg->type){
        case  MSG_TYPE_TELEMETRY:
            LOG_DBG("  payload: %s = %.2f %s",
                msg->payload.telemetry.key,
                msg->payload.telemetry.value,
                msg->payload.telemetry.unit);
            break;
        
        case MSG_TYPE_COMMAND:
            LOG_DBG("  payload: cmd=%s params=%s",
                msg->payload.command.cmd,
                msg->payload.command.params);
            break;
        
        case MSG_TYPE_EVENT:
            LOG_DBG("  payload: [%s]: %s, severity=%d",
                msg->payload.event.name,
                msg->payload.event.detail,
                msg->payload.event.severity);     
            break;

        default:
            break;
    }
    LOG_DBG("---------------");


}