#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

void signal_handler_init(void);

int signal_should_exit(void);

int signal_should_reload(void);

void signal_clear_reload(void);

#endif