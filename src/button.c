#include "button.h"

#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/gpio.h>

#define LONG_PRESS_NS   (1500LL * 1000000LL) /* 1.5 seconds */
#define DOUBLECLICK_NS  (350LL * 1000000LL)  /* 350 ms */

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_ms_short(unsigned ms)
{
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}

static bool check_debugfs_pressed(int fd, unsigned pin)
{
    char buf[8192];
    ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return false;
    buf[n] = '\0';

    char pat[32];
    snprintf(pat, sizeof(pat), "gpio%u", pin);
    char *p = strstr(buf, pat);
    if (!p) {
        snprintf(pat, sizeof(pat), "gpio-%u", 512 + pin);
        p = strstr(buf, pat);
    }
    if (p) {
        char line[128] = {0};
        char *end = strchr(p, '\n');
        if (end && (size_t)(end - p) < sizeof(line)) {
            memcpy(line, p, (size_t)(end - p));
        } else {
            snprintf(line, sizeof(line), "%s", p);
        }
        /* Active-low: pressed if state contains "low" or "lo " */
        if (strstr(line, " low") || strstr(line, " lo ") || strstr(line, " lo\t"))
            return true;
    }
    return false;
}

bool button_init(button_t *b, unsigned gpio_pin)
{
    memset(b, 0, sizeof(*b));
    b->fd = -1;
    b->sysfs_value_fd = -1;
    b->debugfs_fd = -1;
    b->gpio_pin = gpio_pin;

    /* 1. Try cdev line-event */
    for (unsigned chip = 0; chip < 4; chip++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/gpiochip%u", chip);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        struct gpiochip_info info;
        memset(&info, 0, sizeof(info));
        if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0 &&
            gpio_pin < info.lines) {

            struct gpioevent_request req;
            memset(&req, 0, sizeof(req));
            req.lineoffset = gpio_pin;
            req.handleflags = GPIOHANDLE_REQUEST_INPUT;
            req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
            snprintf(req.consumer_label, sizeof(req.consumer_label), "athena-btn");

            if (ioctl(fd, GPIO_GET_LINEEVENT_IOCTL, &req) == 0) {
                close(fd);
                int flags = fcntl(req.fd, F_GETFL);
                fcntl(req.fd, F_SETFL, flags | O_NONBLOCK);
                b->fd = req.fd;
                return true;
            }
        }
        close(fd);
    }

    /* 2. Try sysfs export (both pin and base+pin) */
    unsigned candidates[2] = { 512 + gpio_pin, gpio_pin };
    for (int i = 0; i < 2; i++) {
        char path[96];
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", candidates[i]);
        int vfd = open(path, O_RDONLY | O_CLOEXEC);
        if (vfd < 0) {
            int efd = open("/sys/class/gpio/export", O_WRONLY | O_CLOEXEC);
            if (efd >= 0) {
                char s[16];
                int l = snprintf(s, sizeof(s), "%u", candidates[i]);
                write(efd, s, (size_t)l);
                close(efd);
            }
            vfd = open(path, O_RDONLY | O_CLOEXEC);
        }
        if (vfd >= 0) {
            b->sysfs_value_fd = vfd;
            return true;
        }
    }

    /* 3. Robust Fallback: debugfs parser */
    int dfd = open("/sys/kernel/debug/gpio", O_RDONLY | O_CLOEXEC);
    if (dfd >= 0) {
        b->debugfs_fd = dfd;
        return true;
    }

    return false;
}

static void on_edge(button_t *b, bool pressed_now, button_action_t *out)
{
    long long now = now_ns();

    if (pressed_now) {
        if (b->awaiting_double && now - b->release_ns < DOUBLECLICK_NS) {
            b->awaiting_double = false;
            b->gesture_done = true;
            *out = BUTTON_DOUBLE;
        } else {
            b->gesture_done = false;
        }
        b->pressed = true;
        b->press_start_ns = now;
    } else {
        if (!b->pressed) return;
        b->pressed = false;
        if (b->gesture_done) return;
        b->release_ns = now;
        b->awaiting_double = true;
    }
}

static button_action_t classify(button_t *b)
{
    long long now = now_ns();

    if (b->pressed && !b->gesture_done &&
        now - b->press_start_ns >= LONG_PRESS_NS) {
        b->gesture_done = true;
        return BUTTON_LONG;
    }
    if (b->awaiting_double && now - b->release_ns >= DOUBLECLICK_NS) {
        b->awaiting_double = false;
        return BUTTON_SHORT;
    }
    return BUTTON_NONE;
}

button_action_t button_poll(button_t *b, int timeout_ms)
{
    button_action_t act = BUTTON_NONE;

    /* Backend 1: cdev */
    if (b->fd >= 0) {
        struct pollfd pfd = { .fd = b->fd, .events = POLLIN };
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            struct gpioevent_data ev;
            while (read(b->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.id == GPIOEVENT_EVENT_FALLING_EDGE)
                    on_edge(b, true, &act);
                else if (ev.id == GPIOEVENT_EVENT_RISING_EDGE)
                    on_edge(b, false, &act);
            }
        }
        if (act == BUTTON_NONE) act = classify(b);
        return act;
    }

    /* Backend 2: sysfs */
    if (b->sysfs_value_fd >= 0) {
        char c = '1';
        if (pread(b->sysfs_value_fd, &c, 1, 0) == 1) {
            bool pressed_now = (c == '0');
            if (pressed_now != b->pressed) on_edge(b, pressed_now, &act);
        }
        if (act == BUTTON_NONE) act = classify(b);
        sleep_ms_short((unsigned)timeout_ms);
        return act;
    }

    /* Backend 3: debugfs polling (50ms slice) */
    if (b->debugfs_fd >= 0) {
        int remaining = timeout_ms;
        const int slice = 50;
        while (remaining > 0) {
            bool pressed_now = check_debugfs_pressed(b->debugfs_fd, b->gpio_pin);
            if (pressed_now != b->pressed)
                on_edge(b, pressed_now, &act);

            if (act == BUTTON_NONE)
                act = classify(b);

            if (act != BUTTON_NONE)
                return act;

            int step = remaining > slice ? slice : remaining;
            sleep_ms_short((unsigned)step);
            remaining -= step;
        }
        if (act == BUTTON_NONE) act = classify(b);
        return act;
    }

    sleep_ms_short((unsigned)timeout_ms);
    return BUTTON_NONE;
}

void button_close(button_t *b)
{
    if (b->fd >= 0) close(b->fd);
    if (b->sysfs_value_fd >= 0) close(b->sysfs_value_fd);
    if (b->debugfs_fd >= 0) close(b->debugfs_fd);
    b->fd = -1;
    b->sysfs_value_fd = -1;
    b->debugfs_fd = -1;
}
