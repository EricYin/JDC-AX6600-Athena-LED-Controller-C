#ifndef ATHENA_BUTTON_H
#define ATHENA_BUTTON_H

#include <stdbool.h>

#define BUTTON_GPIO_PIN 71u /* Athena "screen" key on TLMM 71 */

typedef enum {
    BUTTON_NONE = 0,
    BUTTON_SHORT,   /* next page */
    BUTTON_DOUBLE,  /* first page */
    BUTTON_LONG,    /* toggle screen power */
} button_action_t;

typedef struct {
    int fd;                  /* GPIO line-event fd, -1 if sysfs fallback */
    int sysfs_value_fd;      /* sysfs value fd, -1 if cdev */
    bool pressed;
    long long press_start_ns;
    long long release_ns;    /* for double-click window */
    bool awaiting_double;
    bool long_fired;
} button_t;

/* Try cdev line-event first, then sysfs input fallback. */
bool button_init(button_t *b, unsigned gpio_pin);
void button_close(button_t *b);
/* Process pending events; returns a completed gesture or BUTTON_NONE. */
button_action_t button_poll(button_t *b, int timeout_ms);

#endif
