#include "plugin.h"
#include "logger.h"
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static msg_recv_cb_t  g_pushlish_cb  = NULL;
static void         *g_recv_userdata = NULL;
static pthread_t      g_tid;
static volatile int   g_running  = 0;
static uint32_t       g_seq      = 0;

static void *fake_poll_thread(void *arg){
    while (g_running) {
        gateway_msg_t msg = {0};

        // 填写消息
        strncpy(msg.topic, "device/fake/temperature", sizeof(msg.topic) - 1);
        msg.type         = MSG_TYPE_TELEMETRY;
        msg.timestamp_ms = msg_timestamp_ms();
        msg.seq          = g_seq++;

        strncpy(msg.payload.telemetry.key,  "temperature", 32 - 1);
        strncpy(msg.payload.telemetry.unit, "℃",           16 - 1);
        // 模拟25~26度之间的波动
        msg.payload.telemetry.value = 25.0 + (g_seq % 10) * 0.2;

        // 推入总线
        if (g_pushlish_cb) g_pushlish_cb(&msg,NULL);

        sleep(1);
    }
}

// --- 实现 plugin_ops_t 的每个函数 ---

static int fake_init(const char* json_cfg, const msg_recv_cb_t  publish_fn, const subscribe_fn_t subscribe_fn,const unsubscribe_fn_t unsubscribe_fn)
{
    LOG_INF("[fake] init, cfg=%s", json_cfg ? json_cfg : "null");
    g_pushlish_cb = publish_fn;
    (void)subscribe_fn;
    (void)unsubscribe_fn;
    return 0;
}

static int fake_start(void)
{
    g_running = 1;
    pthread_create(&g_tid, NULL, fake_poll_thread, NULL);
    LOG_INF("[fake] started");
    return 0;
}

static void fake_stop(void)
{
    g_running = 0;
    pthread_join(g_tid, NULL);
    LOG_INF("[fake] stopped");
}

static void fake_destroy(void)
{
    LOG_INF("[fake] destroyed");
}

static int fake_send(const gateway_msg_t *msg)
{
    LOG_INF("[fake] received command: %s", msg->payload.command.cmd);
    return 0;
}

static const char *fake_name(void)    { return "fake"; }
static const char *fake_version(void) { return "2.0.0"; }

// --- 导出操作集 ---
static plugin_ops_t g_ops = {
    .init        = fake_init,
    .start       = fake_start,
    .stop        = fake_stop,
    .destroy     = fake_destroy,
    .send        = fake_send,
    .get_name        = fake_name,
    .get_version     = fake_version,
};

// 唯一导出符号，dlsym 通过这个名字找到入口
plugin_ops_t *plugin_entry(void) { return &g_ops; }
