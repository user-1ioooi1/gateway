#include "message_bus.h"
#include "logger.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define NUM_PRODUCERS   16      // 8个生产者线程
#define MSG_PER_PRODUCER 10000 // 每个生产者发10000条
#define TOTAL_MSGS (NUM_PRODUCERS * MSG_PER_PRODUCER)

// 统计收到的消息数
static atomic_int g_recv_count = 0;
// 统计重复的 seq
static atomic_int g_dup_count  = 0;
// 记录收到过的 seq（用于检测丢失和重复）
static atomic_int g_seq_bitmap[TOTAL_MSGS];

// 消息总线回调
static int on_message(gateway_msg_t *msg, void *userdata)
{
    atomic_fetch_add(&g_recv_count, 1);

    // 检查 seq 是否重复
    int seq = (int)msg->seq;
    if (seq >= 0 && seq < TOTAL_MSGS) {
        int old = atomic_fetch_add(&g_seq_bitmap[seq], 1);
        if (old > 0) {
            atomic_fetch_add(&g_dup_count, 1);
        }
    }
    return 0;
}

// 生产者线程
typedef struct {
    int thread_id;
    int start_seq;
} producer_arg_t;

static void *producer_thread(void *arg)
{
    producer_arg_t *a = (producer_arg_t *)arg;

    for (int i = 0; i < MSG_PER_PRODUCER; i++) {
        gateway_msg_t msg = {0};
        msg.type         = MSG_TYPE_TELEMETRY;
        msg.seq          = a->start_seq + i;
        msg.timestamp_ms = msg_timestamp_ms();

        snprintf(msg.topic, sizeof(msg.topic),
                 "device/stress/thread%d", a->thread_id);
        msg.payload.telemetry.value = (double)(a->start_seq + i);

        // 队列满时重试
        while (message_bus_publish(&msg, NULL) != 0)
            usleep(100);
    }
    return NULL;
}

void bus_stress_test(void)
{
    LOG_INF("=== Bus Stress Test ===");
    LOG_INF("Producers: %d, Msgs each: %d, Total: %d",
            NUM_PRODUCERS, MSG_PER_PRODUCER, TOTAL_MSGS);

    // 订阅
    message_bus_subscribe("device/stress/#", on_message, NULL);

    // 启动生产者线程
    pthread_t      tids[NUM_PRODUCERS];
    producer_arg_t args[NUM_PRODUCERS];

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        args[i].thread_id = i;
        args[i].start_seq = i * MSG_PER_PRODUCER;
        pthread_create(&tids[i], NULL, producer_thread, &args[i]);
    }

    // 等待所有生产者完成
    for (int i = 0; i < NUM_PRODUCERS; i++)
        pthread_join(tids[i], NULL);

    // 等待消费者处理完
    int wait = 0;
    while (atomic_load(&g_recv_count) < TOTAL_MSGS && wait < 10) {
        sleep(1);
        wait++;
    }

    // 统计结果
    int recv  = atomic_load(&g_recv_count);
    int dups  = atomic_load(&g_dup_count);
    int lost  = TOTAL_MSGS - recv;

    LOG_INF("=== Results ===");
    LOG_INF("  Expected : %d", TOTAL_MSGS);
    LOG_INF("  Received : %d", recv);
    LOG_INF("  Lost     : %d", lost);
    LOG_INF("  Duplicate: %d", dups);

    if (lost == 0 && dups == 0)
        LOG_INF("  Result   : PASS ✅");
    else
        LOG_ERR("  Result   : FAIL ❌");

    LOG_INF("===============");
}