#include "plugin.h"
#include "logger.h"
#include "mosquitto.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>


typedef struct{
    char subscribe_topics[32][64];
    int subscribe_count;
    char broker_host[128];
    int broker_port;
    char client_id[64];
    int keepalive;
    char topic_prefix[64];
    int qos;
} mqtt_cfg_t;

static struct mosquitto *g_mosq     = NULL;
static mqtt_cfg_t       g_cfg       = { 0 };
static volatile int     g_running   =  0;
static volatile int     g_connected  = 0;
static pthread_t       g_tid;
static unsubscribe_fn_t g_unsubscribe = NULL;


static int parse_cfg(const char *json_str, mqtt_cfg_t *cfg){
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERR("[mqtt] invalid json cfg");
        return -1;
    }

    cJSON *host      = cJSON_GetObjectItem(root, "broker_host");
    cJSON *port      = cJSON_GetObjectItem(root, "broker_port");
    cJSON *cid       = cJSON_GetObjectItem(root, "client_id");
    cJSON *ka        = cJSON_GetObjectItem(root, "keepalive");
    cJSON *prefix    = cJSON_GetObjectItem(root, "topic_prefix");
    cJSON *qos       = cJSON_GetObjectItem(root, "qos");
    cJSON *s_topics  = cJSON_GetObjectItem(root, "subscribe_topics");

 
    if (!cJSON_IsString(host)) {
        LOG_ERR("[mqtt] missing broker_host");
        cJSON_Delete(root);
        return -1;
    }

    strncpy(cfg->broker_host, host->valuestring,
            sizeof(cfg->broker_host) - 1);
    cfg->broker_port = cJSON_IsNumber(port) ?
                       (int)port->valuedouble : 1883;
    cfg->keepalive   = cJSON_IsNumber(ka) ?
                       (int)ka->valuedouble : 60;
    cfg->qos         = cJSON_IsNumber(qos) ?
                       (int)qos->valuedouble : 1;

    if (cJSON_IsString(cid))
        strncpy(cfg->client_id, cid->valuestring,
                sizeof(cfg->client_id) - 1);
    else
        strncpy(cfg->client_id, "gateway-001",
                sizeof(cfg->client_id) - 1);

    if (cJSON_IsString(prefix))
        strncpy(cfg->topic_prefix, prefix->valuestring,
                sizeof(cfg->topic_prefix) - 1);
    else
        strncpy(cfg->topic_prefix, "gw",
                sizeof(cfg->topic_prefix) - 1);
    
    cfg->subscribe_count = 0;
    if (cJSON_IsArray(s_topics)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, s_topics) {
           if(!cJSON_IsString(item)){
                LOG_WRN("[mqtt] subscribe topic is not string");
                continue;
           }
           strncpy(cfg->subscribe_topics[cfg->subscribe_count++], item->valuestring,sizeof(cfg->subscribe_topics[0]) - 1);  
        }
    }else{
        LOG_WRN("[mqtt] no subscribe topics ");
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    LOG_INF("[mqtt] cfg: broker=%s:%d client_id=%s",
            cfg->broker_host, cfg->broker_port, cfg->client_id);
    return 0;
}


static void on_connect(struct mosquitto *mosq, void *userdata, int rc){
    if(rc  == 0){
        g_connected = 1;
        LOG_INF("[mqtt] connected to broker %s:%d",
            g_cfg.broker_host, g_cfg.broker_port);
    }else{
        g_connected = 0;
        LOG_WRN("[mqtt] connect failed rc=%d: %s",
            rc, mosquitto_strerror(rc));
    }
}

static void on_disconnect(struct mosquitto *mosq, void *userdata, int rc){
    g_connected = 0;
    if (rc != 0)
        LOG_WRN("[mqtt] unexpected disconnect rc=%d", rc);
    else
        LOG_INF("[mqtt] disconnected");
}

static void on_publish(struct mosquitto *mosq,
    void *userdata, int mid)
{
    LOG_DBG("[mqtt] published mid=%d", mid);
}


//保持连接 + 断线重连
// 1. 指数退避函数
static int calc_retry_interval(int retry_count)
{
    int interval = 5 * (1 << retry_count);  // 5,10,20,40...
    return interval > 60 ? 60 : interval;   // 上限60秒
}

// 2. mqtt_loop_thread
static void *mqtt_loop_thread(void *arg)
{
    int retry_count = 0;

    while (g_running) {
        if (!g_connected) {
            int interval = calc_retry_interval(retry_count);

            if (retry_count == 0)
                LOG_INF("[mqtt] connecting to %s:%d ...",
                        g_cfg.broker_host, g_cfg.broker_port);
            else
                LOG_WRN("[mqtt] retry #%d in %ds ...",
                        retry_count, interval);

            int rc = mosquitto_connect(g_mosq,
                                       g_cfg.broker_host,
                                       g_cfg.broker_port,
                                       g_cfg.keepalive);
            if (rc != MOSQ_ERR_SUCCESS) {
                LOG_WRN("[mqtt] connect failed: %s",
                        mosquitto_strerror(rc));
                retry_count++;
                sleep(interval);
                continue;
            }

            // 等待 on_connect，最多10秒
            int wait = 0;
            while (!g_connected && g_running && wait < 100) {
                mosquitto_loop(g_mosq, 100, 1);
                wait++;
            }

            if (!g_connected) {
                LOG_WRN("[mqtt] connect timeout");
                mosquitto_disconnect(g_mosq);
                retry_count++;
                sleep(interval);
                continue;
            }

            // 连接成功，重置重试计数
            retry_count = 0;
            LOG_INF("[mqtt] connected to %s:%d",
                    g_cfg.broker_host, g_cfg.broker_port);
        }

        int rc = mosquitto_loop(g_mosq, 100, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_WRN("[mqtt] disconnected: %s",
                    mosquitto_strerror(rc));
            g_connected = 0;
            retry_count = 0;  // 断线重置，从5秒开始重试
        }
    }
    return NULL;
}

//上行回调：消息总线 → MQTT 上报
static int on_bus_message(gateway_msg_t *msg, void *userdata){
    if(!g_connected){
        LOG_WRN("[mqtt] not connected, drop topic=%s", msg->topic);
        return - 1;
    }
    // 把消息序列化成 JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "topic", msg->topic);
    cJSON_AddNumberToObject(root, "ts",    (double)msg->timestamp_ms);
    cJSON_AddNumberToObject(root, "seq",   msg->seq);

    if(msg->type == MSG_TYPE_TELEMETRY){
        cJSON_AddStringToObject(root,"key",msg->payload.telemetry.key);
        cJSON_AddNumberToObject(root,"value",msg->payload.telemetry.value);
        cJSON_AddStringToObject(root,"unit",msg->payload.telemetry.unit);

    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!json_str){
        return -1;
    }

    char mqtt_topic[128];
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", g_cfg.topic_prefix,msg->topic);
    int rc = mosquitto_publish(g_mosq,
                               NULL,
                               mqtt_topic,
                               (int)strlen(json_str),
                               json_str,
                               g_cfg.qos,
                               false);

    if(rc != MOSQ_ERR_SUCCESS){
        LOG_WRN("[mqtt] publish failed: %s", mosquitto_strerror(rc));
        return -1;
    }else{
        LOG_DBG("[mqtt] published topic=%s", mqtt_topic);
    }

    free(json_str);

    return 0;
}

static int mqtt_plugin_init(const char* json_cfg, const msg_recv_cb_t  publish_fn, const subscribe_fn_t subscribe_fn,const unsubscribe_fn_t unsubscribe_fn){
    if(parse_cfg(json_cfg,&g_cfg) != 0){
        return -1;
    }

    mosquitto_lib_init();
    g_mosq = mosquitto_new(g_cfg.client_id,true,NULL);
    if(!g_mosq){
        LOG_ERR("[mqtt] mosquitto_new failed");
        return -1;
    }
    mosquitto_connect_callback_set(g_mosq, on_connect);
    mosquitto_disconnect_callback_set(g_mosq, on_disconnect);
    mosquitto_publish_callback_set(g_mosq, on_publish);

    (void)publish_fn;
    g_unsubscribe = unsubscribe_fn;
    for(int i = 0; i <  g_cfg.subscribe_count; i++){
        subscribe_fn(g_cfg.subscribe_topics[i], on_bus_message,NULL);
    }

    LOG_INF("[mqtt] initialized");
    return 0;
}

static int mqtt_plugin_start(void)
{
    g_running = 1;
    pthread_create(&g_tid, NULL, mqtt_loop_thread, NULL);
    LOG_INF("[mqtt] loop thread started");
    return 0;
}

static void mqtt_plugin_stop(void)
{
    g_running = 0;
    mosquitto_disconnect(g_mosq);
    pthread_join(g_tid, NULL);
    LOG_INF("[mqtt] stopped");
}

static void mqtt_plugin_destroy(void)
{
    if (g_mosq) {
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }
    for(int i = 0; i <  g_cfg.subscribe_count; i++){
        g_unsubscribe(g_cfg.subscribe_topics[i], on_bus_message);
    }
    mosquitto_lib_cleanup();
    LOG_INF("[mqtt] destroyed");
}

static int mqtt_plugin_send(const gateway_msg_t *msg)
{
    // 预留：云端下发指令
    LOG_INF("[mqtt] send not implemented yet");
    return 0;
}


static const char *mqtt_name(void)    { return "mqtt"; }
static const char *mqtt_version(void) { return "1.0.0"; }


static plugin_ops_t g_ops = {
    .init        = mqtt_plugin_init,
    .start       = mqtt_plugin_start,
    .stop        = mqtt_plugin_stop,
    .destroy     = mqtt_plugin_destroy,
    .send        = mqtt_plugin_send,
    .get_name        = mqtt_name,
    .get_version     = mqtt_version,
};

plugin_ops_t *plugin_entry(void) { return &g_ops; }