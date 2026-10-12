#ifndef ATHENA_SCREEN_H
#define ATHENA_SCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_COLS 27

typedef enum {
    GPIO_BACKEND_AUTO,
    GPIO_BACKEND_CDEV,
    GPIO_BACKEND_SYSFS,
} gpio_backend_t;

typedef struct {
    int fd;              /* cdev request fd, -1 if sysfs */
    int fd_stb_l;        /* sysfs value fds, -1 if cdev */
    int fd_stb_r;
    int fd_clk;
    int fd_dio;
    bool powered;
    uint8_t brightness;  /* 0..7 */
} screen_t;

/* status bits used by the 4 discrete indicator LEDs */
#define LED_CLOCK_BIT  0x01
#define LED_MEDAL_BIT  0x02
#define LED_UP_BIT     0x04
#define LED_DOWN_BIT   0x08

bool screen_init(screen_t *s, gpio_backend_t backend, long gpio_base);
void screen_close(screen_t *s);
bool screen_power(screen_t *s, bool on, uint8_t brightness);
bool screen_write_frame(const screen_t *s, const uint8_t cols[SCREEN_COLS], uint8_t status);
long screen_detect_gpio_base(void);

#endif
