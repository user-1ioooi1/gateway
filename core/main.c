#include "logger.h"
#include "config.h"
#include "plugin_manager.h"
#include "message_bus.h"
#include "file_util.h"
#include "signal_handler.h"
#include <unistd.h>
#include <string.h>

void handle_load(const char *config_path,gateway_cfg_t *cfg){
    LOG_INF("=== load triggered ===");
    if (config_update(config_path, cfg) != 0) {
        LOG_ERR("Failed to update config, abort load");
        return;
    }
    log_set_level((log_level_t)cfg->log_level);
    config_show(cfg);

    for(int i = 0; i < MAX_PLUGINS; i++){
        plugin_cfg_t *p = &cfg->plugins[i];
        if(p->state == Unused) continue;
        if(p->state == Update){
            if(plugin_manager_load(p->name,p->so_path,p->json_cfg) != 0){
                LOG_ERR("Failed to load plugin: %s", p->name);
                return;
            }
            if(plugin_manager_start_one(p->name) != 0){
                LOG_ERR("Failed to start plugin: %s", p->name);
                return;
            };
            p->state = Retain;
        }
        else if(p->state == Delete){
            plugin_manager_destroy_one(p->name);
            p->state = Unused;
        }else if(p->state == Retain){
            int ret = 0;
            if((ret = plugin_manager_change_version(p->name,p->so_path,file_get_mtime(p->so_path), p->json_cfg)) == 1){
                ;
            }else if(ret == -1){
                LOG_ERR("Failed to load plugin: %s", p->name);
                return;
            }else if(ret == 0){
                if(plugin_manager_start_one(p->name) != 0){
                    LOG_ERR("Failed to start plugin: %s", p->name);
                    return;
                };
            }
            p->state = Retain;
        }
    }

    return;
}

int on_telemetry(gateway_msg_t *msg, void *userdata){
    LOG_DBG("[MAIN] type = %d, key = %s, value = %lf, unit = %s", msg->type,msg->payload.telemetry.key, msg->payload.telemetry.value, msg->payload.telemetry.unit);
}

int main(int argc, char *argv[])
{
    gateway_cfg_t cfg = {0};
    config_init(&cfg);
    signal_handler_init();
    message_bus_init();

    if (argc > 1 && strcmp(argv[1], "--stress") == 0) {
        // 压测模式
        bus_stress_test();
        message_bus_stop();
        return 0;
    }

    //message_bus_subscribe("device/#", on_telemetry, NULL); //订阅"device/#"
    plugin_manager_init();
    plugin_manager_set_bus_cb(message_bus_publish,message_bus_subscribe, message_bus_unsubscribe); //设置消息总线的回调
   // LOG_INF("Gateway running, pid=%d", getpid());
    handle_load("../config/gateway.json", &cfg);
    LOG_INF("Gateway running, pid=%d", getpid());
    LOG_INF("Send SIGUSR1 to reload: kill -SIGUSR1 %d", getpid());


    while (!signal_should_exit()) {
        if (signal_should_reload()) {
            signal_clear_reload();
            handle_load("../config/gateway.json", &cfg);
        }
        sleep(1);
    }

    LOG_INF("Shutting down...");

    plugin_manager_stop_all();

    plugin_manager_destroy();

    message_bus_stop();

    LOG_INF("Gateway exit");

    return 0;
}