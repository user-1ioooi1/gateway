#include "signal_handler.h"
#include "logger.h"
#include <signal.h>
#include <stdatomic.h>

static atomic_int g_should_exit = 0;
static atomic_int g_should_reload = 0;


static void on_sigterm(int sig)
{
    LOG_INF("Received SIGTERM/SIGINT, exiting...");
    atomic_store(&g_should_exit, 1);
}

static void on_sigusr1(int sig)
{
    LOG_INF("Received SIGUSR1, triggering reload...");
    atomic_store(&g_should_reload, 1);
}

void signal_handler_init(void){
    struct sigaction sa_term = {
        .sa_handler = on_sigterm,
        .sa_flags = 0,
    };
    sigemptyset(&sa_term.sa_mask);
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT, &sa_term, NULL);

    struct sigaction sa_usr1 = {
        .sa_handler = on_sigusr1,
        .sa_flags = 0,
    };

    sigemptyset(&sa_usr1.sa_mask);
    sigaction(SIGUSR1, &sa_usr1, NULL);
    LOG_INF("Signal handler initialized");
}

int signal_should_exit(void){
    return atomic_load(&g_should_exit);
}

int signal_should_reload(void){
    return atomic_load(&g_should_reload);
}

void signal_clear_reload(void){
    atomic_store(&g_should_reload, 0);
}