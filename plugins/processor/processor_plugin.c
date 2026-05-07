#include "plugin.h"
#include "logger.h"
#include "message_bus.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>


#define MAX_RULES 16


typedef struct {
    char   topic_filter[64];  // 匹配哪些 topic //模块自身
    double min;               // 最小值
    double max;               // 最大值
    char   alarm_topic[64];   // 告警消息发到哪个 topic
    int    severity;          // 严重程度 0=info 1=warn 2=error
} alarm_rule_t;

typedef struct {
    char subscribe_topics[32][64];
    int subscribe_count;
    alarm_rule_t rules[MAX_RULES];
    int count;
}processor_cfg_t;



static processor_cfg_t g_cfg     = {0};
static msg_recv_cb_t   g_publish_cb = NULL;  // 消息总线 publish
static void           *g_publish_ud = NULL;
static unsubscribe_fn_t g_unsubscribe = NULL;
static uint32_t        g_seq     = 0;



static int parse_cfg(const char *json_str, processor_cfg_t *cfg)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERR("[processor] invalid json cfg");
        return -1;
    }

    cJSON *s_topics  = cJSON_GetObjectItem(root, "subscribe_topics");
    cJSON *rules = cJSON_GetObjectItem(root, "rules");
    cfg->subscribe_count = 0;


    if(cJSON_IsArray(s_topics)){
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, s_topics) {
           if(!cJSON_IsString(item)){
                LOG_WRN("[mqtt] subscribe topic is not string");
                continue;
           }
           strncpy(cfg->subscribe_topics[cfg->subscribe_count++], item->valuestring,sizeof(cfg->subscribe_topics[0]) - 1);  
        }
    }

    if (!cJSON_IsArray(rules)) {
        LOG_ERR("[processor] missing rules array");
        cJSON_Delete(root);
        return -1;
    }
    cJSON *item = NULL;
    cfg->count = 0;
    cJSON_ArrayForEach(item, rules) {
        if (cfg->count >= MAX_RULES) break;

        alarm_rule_t *r = &cfg->rules[cfg->count];

        cJSON *filter   = cJSON_GetObjectItem(item, "topic_filter");
        cJSON *min      = cJSON_GetObjectItem(item, "min");
        cJSON *max      = cJSON_GetObjectItem(item, "max");
        cJSON *atopic   = cJSON_GetObjectItem(item, "alarm_topic");
        cJSON *severity = cJSON_GetObjectItem(item, "severity");

        if (!cJSON_IsString(filter)) continue;

        strncpy(r->topic_filter, filter->valuestring,
                sizeof(r->topic_filter) - 1);

        r->min      = cJSON_IsNumber(min) ?
                      min->valuedouble : -999999;
        r->max      = cJSON_IsNumber(max) ?
                      max->valuedouble :  999999;
        r->severity = cJSON_IsNumber(severity) ?
                      (int)severity->valuedouble : 1;

        if (cJSON_IsString(atopic))
            strncpy(r->alarm_topic, atopic->valuestring,
                    sizeof(r->alarm_topic) - 1);
        else
            strncpy(r->alarm_topic, "alarm/default",
                    sizeof(r->alarm_topic) - 1);

        LOG_INF("[processor] rule[%d]: filter=%s min=%.1f max=%.1f",
                cfg->count, r->topic_filter, r->min, r->max);

        cfg->count++;
    }

    cJSON_Delete(root);
    return 0;
}


static int topic_match(const char *filter, const char *topic){
    if(filter == NULL || topic == NULL){
        LOG_ERR("filter or topic NULL");
        return 0; //不成功
    }
    
    while(*filter && *topic){
        if (*filter == '#') {
            return 1;   // # 匹配剩下所有，直接成功
        }else if(*filter == '+'){
            while(*topic && *topic != '/') topic++;
            filter++;
        }else if(*filter == *topic){
            filter++;
            topic++;
        }else{
            return 0;
        }
    }

    if(*filter == '#'){ //末尾可能是# a/b/# 匹配 a/b
        return 1;
    }

    return (*filter == '\0' && *topic == '\0');

}


static void check_rule(const gateway_msg_t *msg, const alarm_rule_t  *rule){
    double val = msg->payload.telemetry.value;
    // 在范围内，正常
    if (val >= rule->min && val <= rule->max) return;

    // 超限，构造告警消息
    gateway_msg_t alarm = {0};
    alarm.type         = MSG_TYPE_EVENT;
    alarm.timestamp_ms = msg_timestamp_ms();
    alarm.seq          = g_seq++;

    strncpy(alarm.topic, rule->alarm_topic,
            sizeof(alarm.topic) - 1);
    alarm.payload.event.severity = rule->severity;

    // 告警名
    strncpy(alarm.payload.event.name, "threshold_exceeded",
            sizeof(alarm.payload.event.name) - 1);

    // 告警详情
    snprintf(alarm.payload.event.detail,
             sizeof(alarm.payload.event.detail),
             "topic=%s val=%.2f out of range [%.2f, %.2f]",
             msg->topic, val, rule->min, rule->max);

    static const char *sev_str[] = { "INFO", "WARN", "ERROR" };
    LOG_WRN("[processor] ALARM [%s] %s",
            sev_str[rule->severity],
            alarm.payload.event.detail);

    if (g_publish_cb)
        g_publish_cb(&alarm, g_publish_ud);


}

static int on_bus_message(gateway_msg_t *msg, void *userdata)
{
    // 只处理遥测数据
    if (msg->type != MSG_TYPE_TELEMETRY) return 0;

    for (int i = 0; i < g_cfg.count; i++) {
        alarm_rule_t *r = &g_cfg.rules[i];
        if (topic_match(r->topic_filter, msg->topic)) {
            check_rule(msg, r);
        }
    }
    return 0;
}


static int processor_init(const char* json_cfg, const msg_recv_cb_t  publish_fn, const subscribe_fn_t subscribe_fn,const unsubscribe_fn_t unsubscribe_fn){
    if(parse_cfg(json_cfg,&g_cfg) != 0){
        LOG_ERR("[processor] parse err");
        return -1;
    }
    g_publish_cb = publish_fn;
    g_unsubscribe = unsubscribe_fn;
    for(int i = 0 ; i < g_cfg.count; i++){
        subscribe_fn(g_cfg.subscribe_topics[i], on_bus_message, NULL);
    }
    return 0;
}

static int processor_start(void){
    LOG_INF("[processor] started, rules=%d", g_cfg.count);
    return 0;
}

static void processor_stop(void){
    LOG_INF("[processor] stopped");
}

static void processor_destroy(void){
    for(int i = 0 ; i < g_cfg.count; i++){
        g_unsubscribe(g_cfg.subscribe_topics[i], on_bus_message);
    }
    LOG_INF("[processor] destroyed");
}

static int processor_send(const gateway_msg_t *msg)
{
    return 0;
}

static const char *processor_name(void){ return "processor"; }

static const char *processor_version(void){  return "1.0.0"; }


static plugin_ops_t g_ops = {
    .init        = processor_init,
    .start       = processor_start,
    .stop        = processor_stop,
    .destroy     = processor_destroy,
    .send        = processor_send,
    .get_name        = processor_name,
    .get_version     = processor_version,
};

plugin_ops_t *plugin_entry(void) { return &g_ops; }
