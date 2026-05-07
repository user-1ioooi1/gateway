#include "plugin.h"
#include "logger.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

// SocketCAN 头文件
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define MAX_RULES 32

typedef struct{
    uint32_t can_id;
    char key[32];
    char unit[16];
    char topic[64];
    int start_byte;
    int length;
    double scale;
    bool big_endian;
} can_rule_t;


typedef struct{
    char interface[16]; //接口名
    can_rule_t rules[MAX_RULES];
    int rule_count;
} can_cfg_t;


// ============================================================
// 插件私有状态
// ============================================================
static can_cfg_t     g_cfg     = {0};
static int           g_sockfd  = -1;
static pthread_t     g_tid;
static volatile int  g_running = 0;
static msg_recv_cb_t g_recv_cb = NULL;
static void         *g_recv_ud = NULL;
static uint32_t      g_seq     = 0;


static int parse_cfg(const char *json_str, can_cfg_t *cfg){
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERR("[can] invalid json cfg");
        return -1;
    }

    cJSON *interface = cJSON_GetObjectItem(root, "interface");
    if (!cJSON_IsString(interface)) {
        LOG_ERR("[can] missing interface");
        cJSON_Delete(root);
        return -1;
    }

    strncpy(cfg->interface, interface->valuestring, sizeof(cfg->interface) - 1);

    cJSON *rules = cJSON_GetObjectItem(root, "rules");

    cfg->rule_count = 0;

    if(cJSON_IsArray(rules)){
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, rules) {
            if (cfg->rule_count >= MAX_RULES) break;
            can_rule_t *r = &cfg->rules[cfg->rule_count];
            cJSON *can_id = cJSON_GetObjectItem(item, "can_id");
            cJSON *key = cJSON_GetObjectItem(item, "key");
            cJSON *unit = cJSON_GetObjectItem(item, "unit");
            cJSON *topic = cJSON_GetObjectItem(item, "topic");
            cJSON *start_byte = cJSON_GetObjectItem(item, "start_byte");
            cJSON *length = cJSON_GetObjectItem(item, "length");
            cJSON *big_endian = cJSON_GetObjectItem(item, "big_endian");
            cJSON *scale = cJSON_GetObjectItem(item, "scale");
            cJSON *byte_order = cJSON_GetObjectItem(item, "byte_order");
            if (!cJSON_IsNumber(can_id) || !cJSON_IsString(key)) {
                LOG_WRN("[can] missing can_id or key");
                continue;
            }

            r->can_id = (uint32_t)can_id->valuedouble;
            r->start_byte = cJSON_IsNumber(start_byte) ?
                            (int)start_byte->valuedouble : 0;
            r->length = cJSON_IsNumber(length) ?
                        (int)length->valuedouble : 2;
            r->scale = cJSON_IsNumber(scale) ?
                        scale->valuedouble : 1.0;
            r->big_endian = true; //默认大端
            if (cJSON_IsString(byte_order))
                r->big_endian = strcmp(byte_order->valuestring,
                                       "big") == 0 ? true : false;

            strncpy(r->key, key->valuestring, sizeof(r->key)   - 1);
            strncpy(r->unit,  cJSON_IsString(unit) ?unit->valuestring : "",
                                        sizeof(r->unit)  - 1);
            strncpy(r->topic, cJSON_IsString(topic) ?
                                        topic->valuestring : r->key,
                                        sizeof(r->topic) - 1);
            cfg->rule_count++;
        }
    }
    cJSON_Delete(root);
    LOG_INF("[can] cfg: interface=%s rules=%d",
            cfg->interface, cfg->rule_count);
    return 0;
}

// ============================================================
// 从 CAN 帧数据里按规则提取数值
// ============================================================
static double extract_value(const uint8_t *data,can_rule_t *r){
    uint64_t raw  = 0;
    if(r->big_endian){
        for(int i = 0; i < r->length; i++){
            raw  = (raw << 8) | data[r->start_byte + i];
        }
    }else{
        for (int i = r->length - 1; i >= 0; i--) {
            raw = (raw << 8) | data[r->start_byte + i];  //(raw << 8) 不依赖本机字节序
        }
    }

    return raw * r->scale;
}

static void *can_recv_thread(void *arg){
    struct can_frame frame;

    while(g_running){
        fd_set rfds; 
        FD_ZERO(&rfds);
        FD_SET(g_sockfd, &rfds);
        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        int ret = select(g_sockfd + 1, &rfds, NULL,NULL,&tv); //1s唤醒，避免 read 永久阻塞，支持优雅退出

        if(ret < 0){
            LOG_ERR("[can] select failed: %s", strerror(errno));
            break;
        }
        if(ret == 0) continue;

        ssize_t nbytes = read(g_sockfd, &frame, sizeof(frame));

        if(nbytes < (ssize_t)sizeof(frame)){
            LOG_ERR("[can] read failed: %s", strerror(errno));
            break;
        }

        uint32_t canid = frame.can_id & CAN_EFF_MASK;

        for(int i = 0; i < g_cfg.rule_count; i++){
            can_rule_t *r = &g_cfg.rules[i];
            if(canid != r->can_id) continue;
            
            double val = extract_value(frame.data, r);
            LOG_DBG("[can] canid=%08x key=%s value=%f",
                    canid, r->key, val);

            gateway_msg_t msg = {0};
            msg.type = MSG_TYPE_TELEMETRY;
            msg.timestamp_ms = msg_timestamp_ms();
            msg.seq = g_seq++;
            strncpy(msg.topic, r->topic, sizeof(msg.topic) - 1);
            strncpy(msg.payload.telemetry.key, r->key, sizeof(msg.payload.telemetry.key) - 1);
            strncpy(msg.payload.telemetry.unit, r->unit, sizeof(msg.payload.telemetry.unit) - 1);
            msg.payload.telemetry.value = val;
            
            if (g_recv_cb) g_recv_cb(&msg, g_recv_ud);
            LOG_DBG("[can] id=0x%03x key=%s val=%.2f",
                canid, r->key, val);
        }
    }
    return NULL;
}



static int can_plugin_init(const char* json_cfg, const msg_recv_cb_t  publish_fn, const subscribe_fn_t subscribe_fn,const unsubscribe_fn_t unsubscribe_fn){
    if(parse_cfg(json_cfg,&g_cfg) != 0){
        return -1;
    }

    g_sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW);

    if (g_sockfd < 0) {
        LOG_ERR("[can] socket failed: %s", strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, g_cfg.interface,IFNAMSIZ - 1);
    if(ioctl(g_sockfd, SIOCGIFINDEX, &ifr) < 0){
        LOG_ERR("[can] ioctl failed: %s", strerror(errno));
        close(g_sockfd);
        return -1;
    }

    struct sockaddr_can addr = {
        .can_family = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };

    if(bind(g_sockfd,(struct sockaddr *)&addr, sizeof(addr)) < 0){
        LOG_ERR("[can] bind failed: %s", strerror(errno));
        close(g_sockfd);
        return -1;
    }

    (void)subscribe_fn;
    (void)unsubscribe_fn;
    g_recv_cb = publish_fn;

    LOG_INF("[can] bound to %s", g_cfg.interface);
    return 0;
}



static int can_plugin_start(void)
{
    g_running = 1;
    pthread_create(&g_tid, NULL, can_recv_thread, NULL);
    LOG_INF("[can] recv thread started");
    return 0;
}


static void can_plugin_stop(void)
{
    g_running = 0;
    pthread_join(g_tid, NULL);
    LOG_INF("[can] recv thread stopped");
}


static void can_plugin_destroy(void)
{
    if (g_sockfd >= 0) {
        close(g_sockfd);
        g_sockfd = -1;
    }
    LOG_INF("[can] destroyed");
}

static int can_plugin_send(const gateway_msg_t *msg)
{
    // 预留：发送 CAN 帧
    LOG_INF("[can] send not implemented yet");
    return 0;
}


static const char *can_name(void)         { return "can"; }
static const char *can_version(void)      { return "1.0.0"; }


static plugin_ops_t g_ops = {
    .init        = can_plugin_init,
    .start       = can_plugin_start,
    .stop        = can_plugin_stop,
    .destroy     = can_plugin_destroy,
    .send        = can_plugin_send,
    .get_name        = can_name,
    .get_version     = can_version,
};

plugin_ops_t *plugin_entry(void) { return &g_ops; }