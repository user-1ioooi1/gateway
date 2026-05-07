#include "message_bus.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "plugin.h"


// ============================================================
// 环形队列
// ============================================================

/*mutex 保护：
  多个生产者之间互斥
  同一时刻只有一个生产者能写 head
  防止两个线程读到相同的 head 值

atomic 保护：
  生产者和消费者之间的可见性
  消费者不在 mutex 保护范围内
  需要 memory_order 保证消费者看到完整数据*/

#define BUS_QUEUE_SIZE  64  


typedef struct {
    gateway_msg_t   ring[BUS_QUEUE_SIZE];
    atomic_uint   head;  //消费者读
    atomic_uint   tail; //生产者写
} ringbuf_t;

static ringbuf_t g_queue;
static pthread_mutex_t g_pub_mutex = PTHREAD_MUTEX_INITIALIZER;

static int ringbuf_push(ringbuf_t *rb, const gateway_msg_t *msg)
{
    pthread_mutex_lock(&g_pub_mutex);
    uint32_t tail = atomic_load_explicit(&rb->tail,
        memory_order_relaxed); //explicit显式指定顺序

    uint32_t next = (tail + 1) % BUS_QUEUE_SIZE; 

    if (next == atomic_load_explicit(&rb->head,memory_order_acquire)) { //保证顺序一定正确
        pthread_mutex_unlock(&g_pub_mutex);
        return -1;
    } //队列满了 只保存N-1个数据

    rb->ring[tail] = *msg;  

    atomic_store_explicit(&rb->tail, next, memory_order_release); //确保tail指针更改在写数据后
    pthread_mutex_unlock(&g_pub_mutex);

    return 0;
}

static int ringbuf_pop(ringbuf_t *rb, gateway_msg_t *msg){
    uint32_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    if(head == atomic_load_explicit(&rb->tail,memory_order_acquire)){ //确保后面操作不前移动
        return -1;
    }//队列空  
    // 必须先判断
    //在赋值

    *msg = rb->ring[head];

    atomic_store_explicit(&rb->head, (head + 1) % BUS_QUEUE_SIZE, memory_order_release);

    return 0;
}

// ============================================================
// 订阅者链表
// ============================================================

#define MAX_SUBSCRIBERS 32

typedef struct {
    char          topic_filter[64];
    msg_recv_cb_t cb;
    void         *userdata; //userdata 让同一个回调函数可以服务多个订阅者
    int           used;
} subscriber_t;

static subscriber_t  g_subscribers[MAX_SUBSCRIBERS];
static pthread_mutex_t g_sub_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
//return bool
// Topic 通配符匹配
// MQTT 风格：+ 匹配单层，# 匹配多层
// 例：
//   filter="device/+/temp" 匹配 "device/modbus/temp"
//   filter="device/#"      匹配 "device/modbus/temp/raw"
// ============================================================

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



// ============================================================
// 分发线程
// ============================================================
static pthread_t g_dispatch_tid;
static volatile int g_running = 0; 


static void *dispatch_thread(void *arg){ //取出消息，发送给订阅者，订阅者用cb取走

    gateway_msg_t msg;

    while (g_running){
        if(ringbuf_pop(&g_queue,&msg) != 0){  
            usleep(1000); //等1ms
            continue;
        }
    
        pthread_mutex_lock(&g_sub_mutex);

        for(int i = 0; i < MAX_SUBSCRIBERS; i++){ //给所有匹配的发送消息
            subscriber_t *s = &g_subscribers[i];
            if(!s->used) continue;
            if(topic_match(s->topic_filter,msg.topic)){
                s->cb(&msg,s->userdata);
            }
        }
        pthread_mutex_unlock(&g_sub_mutex);
    }
    return NULL;

}


// ============================================================
// 公开接口
// ============================================================

int  message_bus_init(void){
    memset(&g_queue,       0, sizeof(g_queue));
    memset(g_subscribers,  0, sizeof(g_subscribers));
    atomic_init(&g_queue.head, 0);
    atomic_init(&g_queue.tail, 0);
    g_running = 1;
    pthread_create(&g_dispatch_tid, NULL, dispatch_thread, NULL);
    LOG_INF("Message bus initialized");
    return 0;

}


int message_bus_publish(gateway_msg_t *msg, void *userdata) //生产者
{
    if (ringbuf_push(&g_queue, msg) != 0) {
        LOG_WRN("Message bus full, drop msg topic=%s", msg->topic);
        return -1;
    }
    return 0;
}

int  message_bus_subscribe(const char *topic_filter,msg_recv_cb_t cb,void *userdata){

    pthread_mutex_lock(&g_sub_mutex);
    for(int i = 0; i < MAX_SUBSCRIBERS; i++){
        if(!g_subscribers[i].used){
            subscriber_t *s = &g_subscribers[i];
            strncpy(s->topic_filter, topic_filter, sizeof(s->topic_filter) - 1);
            s->topic_filter[sizeof(s->topic_filter) - 1] = '\0';
            s->cb       = cb;
            s->userdata = userdata;
            s->used     = 1;
            LOG_INF("Subscribed: filter=%s", topic_filter);
            pthread_mutex_unlock(&g_sub_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_sub_mutex);
    LOG_ERR("Subscriber slots full");
    return -1;
 
}


void message_bus_stop(void)
{
    g_running = 0;
    pthread_join(g_dispatch_tid, NULL);
    LOG_INF("Message bus stopped");
}


int message_bus_unsubscribe(const char * topic_filter , msg_recv_cb_t cb){
    pthread_mutex_lock(&g_sub_mutex);
    for(int i = 0; i < MAX_SUBSCRIBERS; i++){
        subscriber_t *s = &g_subscribers[i];
        if(!s->used)continue;
        if (strcmp(s->topic_filter, topic_filter) == 0 &&
            s->cb == cb) {
            memset(s, 0, sizeof(subscriber_t));
            LOG_INF("Unsubscribed: filter=%s", topic_filter);
            pthread_mutex_unlock(&g_sub_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_sub_mutex);
    LOG_ERR("unsubscriber");
    return -1;
}