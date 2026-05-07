#include "plugin.h"
#include "logger.h"
#include "modbus_cfg.h"
#include <modbus/modbus.h>
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>


// ============================================================
// 插件私有状态
// ============================================================
static modbus_t      *g_ctx      = NULL;
static modbus_cfg_t   g_cfg      = {0};
static pthread_t      g_tid;
static volatile int   g_running  = 0;
static msg_recv_cb_t  g_publish_cb  = NULL;
static void          *g_publish_ud  = NULL;
static uint32_t       g_seq      = 0;

static int parse_json(const char *json_str, modbus_cfg_t *cfg){
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERR("[modbus] invalid json cfg");
        return -1;
    }

    cJSON *device   = cJSON_GetObjectItem(root, "device");
    cJSON *baud     = cJSON_GetObjectItem(root, "baud");
    cJSON *slave_id = cJSON_GetObjectItem(root, "slave_id");
    cJSON *interval = cJSON_GetObjectItem(root, "poll_interval_ms");
    cJSON *prefix   = cJSON_GetObjectItem(root, "topic_prefix");
    cJSON *parity = cJSON_GetObjectItem(root, "parity");
    cJSON *data_bit = cJSON_GetObjectItem(root, "data_bit");
    cJSON *stop_bit = cJSON_GetObjectItem(root, "stop_bit");
    cJSON *timeout_s = cJSON_GetObjectItem(root, "response_timeout_s");
    cJSON *timeout_us = cJSON_GetObjectItem(root, "response_timeout_us");

    if (!cJSON_IsString(device) || !cJSON_IsNumber(baud) 
        || !cJSON_IsNumber(data_bit) || !cJSON_IsNumber(stop_bit)) {
        LOG_ERR("[modbus] missing device or baud or stop_bit or data_bit");
        cJSON_Delete(root);
        return -1;
    }

    strncpy(cfg->device, device->valuestring,
        sizeof(cfg->device) - 1);

    cfg->baud     = (int)baud->valuedouble;
    cfg->data_bit = (int)data_bit->valuedouble;
    cfg->stop_bit = (int)stop_bit->valuedouble;
    cfg->slave_id = cJSON_IsNumber(slave_id) ?
                        (uint8_t)slave_id->valuedouble : 1;
    cfg->poll_interval_ms = cJSON_IsNumber(interval) ?
                                (int)interval->valuedouble : 1000;
    cfg->response_timeout_s = cJSON_IsNumber(timeout_s) ?
                                 (int)timeout_s->valuedouble : 1;
    cfg->response_timeout_us = cJSON_IsNumber(timeout_us) ?
                                  (int)timeout_us->valuedouble : 0;
    if(cJSON_IsString(parity)){
        cfg->parity = parity->valuestring[0];
    }else{
        cfg->parity = 'N';
    }
    
    if (cJSON_IsString(prefix))
        strncpy(cfg->topic_prefix, prefix->valuestring,sizeof(cfg->topic_prefix) - 1);
    else
        strncpy(cfg->topic_prefix, "device/modbus",sizeof(cfg->topic_prefix) - 1);

    cfg->topic_prefix[sizeof(cfg->topic_prefix) - 1] = 0;


    cJSON *regs = cJSON_GetObjectItem(root, "registers");
    cfg->reg_count = 0;

    if (cJSON_IsArray(regs)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, regs) {
            if (cfg->reg_count >= MAX_REGS) break;

            reg_def_t *r = &cfg->regs[cfg->reg_count];

            cJSON *addr  = cJSON_GetObjectItem(item, "addr");
            cJSON *key   = cJSON_GetObjectItem(item, "key");
            cJSON *unit  = cJSON_GetObjectItem(item, "unit");
            cJSON *scale = cJSON_GetObjectItem(item, "scale");

            if (!cJSON_IsNumber(addr) || !cJSON_IsString(key))
                continue;

            r->addr  = (uint16_t)addr->valuedouble;
            r->scale = cJSON_IsNumber(scale) ?
                       scale->valuedouble : 1.0;

            strncpy(r->key,  key->valuestring,  sizeof(r->key)  - 1);

            if (cJSON_IsString(unit)){
                strncpy(r->unit, unit->valuestring,sizeof(r->unit) - 1);
            }

            cfg->reg_count++;
        }
    }

    cJSON_Delete(root);
    LOG_INF("[modbus] cfg: device=%s baud=%d slave=%d regs=%d",
            cfg->device, cfg->baud,
            cfg->slave_id, cfg->reg_count);
    return 0;
}


static void *poll_thread(void *arg){
    uint16_t raw[MAX_REGS];

    while (g_running) {
        // 读所有寄存器
        for (int i = 0; i < g_cfg.reg_count; i++) {
            reg_def_t *r = &g_cfg.regs[i];
            raw[i] = 0;
            int rc = modbus_read_registers(g_ctx, r->addr, 1, &raw[i]);

            //LOG_DBG("[modbus] read reg 0x%04x rc=%d raw=%d ,index = %d", r->addr, rc, raw[i], i);

            if (rc != 1) {
                LOG_WRN("[modbus] read reg 0x%04x failed: %s",
                        r->addr, modbus_strerror(errno));
                //usleep(50000);  // 每次读完等50ms，让串口缓冲区清空
                continue;
            } 

            // 封装消息
            gateway_msg_t msg = {0};
            msg.type         = MSG_TYPE_TELEMETRY;
            msg.timestamp_ms = msg_timestamp_ms();
            msg.seq          = g_seq++;

            snprintf(msg.topic, sizeof(msg.topic),
                     "%s/%s", g_cfg.topic_prefix, r->key);

            strncpy(msg.payload.telemetry.key,
                    r->key, sizeof(msg.payload.telemetry.key) - 1);
            strncpy(msg.payload.telemetry.unit,
                    r->unit, sizeof(msg.payload.telemetry.unit) - 1);

            // 原始值 × 缩放系数 = 实际值
            msg.payload.telemetry.value = raw[i] * r->scale;

            // 推入总线
            if (g_publish_cb)
                g_publish_cb(&msg, g_publish_ud);
        }

        usleep(g_cfg.poll_interval_ms * 1000);
    }
    return NULL;
}




static int modbus_plugin_init(const char* json_cfg, const msg_recv_cb_t  publish_fn, const subscribe_fn_t subscribe_fn,const unsubscribe_fn_t unsubscribe_fn){
    if(parse_json(json_cfg,&g_cfg) != 0){
        return -1;
    }
    g_ctx = modbus_new_rtu(g_cfg.device,
        g_cfg.baud,
        g_cfg.parity, g_cfg.data_bit, g_cfg.stop_bit);  // 无校验，8数据位，1停止位
    if (!g_ctx) {
        LOG_ERR("[modbus] modbus_new_rtu failed");
        return -1;
    }
    modbus_set_slave(g_ctx, g_cfg.slave_id);
    modbus_set_response_timeout(g_ctx, g_cfg.response_timeout_s, g_cfg.response_timeout_us); //s,us

    if (modbus_connect(g_ctx) != 0) {
        LOG_ERR("[modbus] connect %s failed: %s",
                g_cfg.device, modbus_strerror(errno));
        modbus_free(g_ctx);
        g_ctx = NULL;
        return -1;
    }

    g_publish_cb = publish_fn;
    (void)subscribe_fn;
    (void)unsubscribe_fn;

    LOG_INF("[modbus] connected to %s", g_cfg.device);
    return 0;
}

static int modbus_plugin_start(void){
    g_running = 1;
    pthread_create(&g_tid, NULL, poll_thread, NULL);
    LOG_INF("[modbus] poll thread started");
    return 0;
}

static void modbus_plugin_stop(void){
    g_running = 0;
    pthread_join(g_tid, NULL);
    LOG_INF("[modbus] poll thread stopped");
}

static void modbus_plugin_destroy(void){
    if (g_ctx) {
        modbus_close(g_ctx);
        modbus_free(g_ctx);
        g_ctx = NULL;
    }
    LOG_INF("[modbus] destroyed");
}



static int modbus_plugin_send(const gateway_msg_t *msg){// 写寄存器
    if (msg->type != MSG_TYPE_COMMAND) return -1;

    uint16_t val = (uint16_t)msg->payload.telemetry.value;
    uint16_t addr = 0x0000;  // 可从 msg->payload.command.params 解析

    if (modbus_write_register(g_ctx, addr, val) != 1) {
        LOG_ERR("[modbus] write reg failed: %s",
                modbus_strerror(errno));
        return -1;
    }
    return 0;
}

static const char *modbus_get_name(void)    { return "modbus"; }
static const char *modbus_get_version(void) { return "1.0.0"; }

// ============================================================
// 导出操作集
// ============================================================
static plugin_ops_t g_ops = {
    .init        = modbus_plugin_init,
    .start       = modbus_plugin_start,
    .stop        = modbus_plugin_stop,
    .destroy     = modbus_plugin_destroy,
    .send        = modbus_plugin_send,
    .get_name        = modbus_get_name,
    .get_version     = modbus_get_version,
};


plugin_ops_t *plugin_entry(void) { return &g_ops; }



