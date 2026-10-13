#ifndef ATHENA_BUTTON_H
#define ATHENA_BUTTON_H

#include <stdbool.h>

typedef enum {
    BUTTON_NONE = 0,
    BUTTON_SHORT,   /* next page */
    BUTTON_DOUBLE,  /* first page */
    BUTTON_LONG,    /* toggle screen power */
} button_action_t;

/* Signal-driven button receiver (procd /etc/rc.button/screen -> SIGUSR1/SIGUSR2).
 * Zero hardware polling, zero file descriptors, purely event-driven. */
void button_init_signals(void);
void button_notify_signal(int sig);
button_action_t button_get_action(void);

#endif
