#ifndef ATHENA_BUTTON_H
#define ATHENA_BUTTON_H

#include <stdbool.h>

#define BUTTON_GPIO_PIN     71u /* Dedicated screen button on TLMM 71 */

typedef enum {
    BUTTON_NONE = 0,
    BUTTON_SHORT,   /* next page */
    BUTTON_DOUBLE,  /* first page */
    BUTTON_LONG,    /* toggle screen power */
} button_action_t;

typedef struct {
    int fd;                  /* GPIO line-event fd, -1 if not cdev */
    int sysfs_value_fd;      /* sysfs value fd, -1 if not sysfs */
    int debugfs_fd;          /* debugfs fd (/sys/kernel/debug/gpio), -1 if not debugfs */
    unsigned gpio_pin;
    bool pressed;
    long long press_start_ns;
    long long release_ns;
    bool awaiting_double;
    bool gesture_done;
} button_t;

/* Initialize button listener with multi-backend fallback:
 * 1. cdev line-event -> 2. sysfs -> 3. debugfs parser */
bool button_init(button_t *b, unsigned gpio_pin);
void button_close(button_t *b);

/* Poll for button events; returns gesture action */
button_action_t button_poll(button_t *b, int timeout_ms);

#endif
