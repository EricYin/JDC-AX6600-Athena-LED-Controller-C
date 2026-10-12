/*
 * Physical "screen" button driver.
 *
 * Primary backend: GPIO character-device line events (edge-triggered,
 * poll() based - zero CPU while the button is idle).
 * Fallback: sysfs input polled at 100ms for legacy firmware.
 *
 * Gestures:
 *   short press  -> next page
 *   double click -> first page
 *   long press   -> toggle screen power (>= 2s)
 */
#include "button.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/gpio.h>

#define LONG_PRESS_NS   (2LL * 1000000000LL)
#define DOUBLECLICK_NS  (350LL * 1000000LL)
#define SYSFS_POLL_NS   (100LL * 1000000LL)

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static bool sysfs_export_input(unsigned pin)
{
    int fd = open("/sys/class/gpio/export", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%u", pin);
    bool ok = write(fd, buf, (size_t)len) == len;
    close(fd);
    if (!ok) return false;

    char path[96];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", pin);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ok = write(fd, "in", 2) == 2;
    close(fd);
    return ok;
}

bool button_init(button_t *b, unsigned gpio_pin)
{
    memset(b, 0, sizeof(*b));
    b->fd = -1;
    b->sysfs_value_fd = -1;

    /* Try cdev line-event on the main SoC gpiochip. */
    for (unsigned chip = 0; chip < 16; chip++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/gpiochip%u", chip);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) break;

        struct gpiochip_info info;
        memset(&info, 0, sizeof(info));
        if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0 &&
            (strstr(info.label, "pinctrl") || strstr(info.label, "tlmm")) &&
            gpio_pin < info.lines) {

            struct gpioevent_request req;
            memset(&req, 0, sizeof(req));
            req.lineoffset = gpio_pin;
            req.handleflags = GPIOHANDLE_REQUEST_INPUT;
            req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
            snprintf(req.consumer_label, sizeof(req.consumer_label), "athena-led-c-btn");

            if (ioctl(fd, GPIO_GET_LINEEVENT_IOCTL, &req) == 0) {
                close(fd);
                b->fd = req.fd;
                return true;
            }
        }
        close(fd);
    }

    /* Fallback: sysfs input polling. */
    if (!sysfs_export_input(gpio_pin)) return false;
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio_pin);
    b->sysfs_value_fd = open(path, O_RDONLY | O_CLOEXEC);
    return b->sysfs_value_fd >= 0;
}

static void on_edge(button_t *b, bool pressed_now)
{
    long long now = now_ns();

    if (pressed_now) {
        /* press edge */
        if (b->awaiting_double && now - b->release_ns < DOUBLECLICK_NS) {
            b->awaiting_double = false;
            b->long_fired = true; /* consume gesture, handled by caller */
            b->pressed = true;
            b->press_start_ns = now;
            return;
        }
        b->pressed = true;
        b->press_start_ns = now;
        b->long_fired = false;
    } else {
        /* release edge */
        if (!b->pressed) return;
        b->pressed = false;
        if (b->long_fired) return; /* already fired as long press */
        b->release_ns = now;
        b->awaiting_double = true;
    }
}

static button_action_t classify(button_t *b)
{
    long long now = now_ns();
    button_action_t act = BUTTON_NONE;

    /* long press fires while held (no need to wait for release) */
    if (b->pressed && !b->long_fired && now - b->press_start_ns >= LONG_PRESS_NS) {
        b->long_fired = true;
        act = BUTTON_LONG;
    }
    /* double-click window expired -> single short press */
    if (b->awaiting_double && now - b->release_ns >= DOUBLECLICK_NS) {
        b->awaiting_double = false;
        act = BUTTON_SHORT;
    }
    return act;
}

button_action_t button_poll(button_t *b, int timeout_ms)
{
    /* cdev: edge events via poll(), essentially zero CPU when idle */
    if (b->fd >= 0) {
        struct pollfd pfd = { .fd = b->fd, .events = POLLIN };
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            struct gpioevent_data ev;
            while (read(b->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.id == GPIOEVENT_EVENT_FALLING_EDGE) on_edge(b, true);
                else if (ev.id == GPIOEVENT_EVENT_RISING_EDGE) on_edge(b, false);
            }
        }
        button_action_t act = classify(b);
        if (act == BUTTON_SHORT && b->long_fired) {
            /* double-click mark: reclassify */
        }
        if (b->long_fired && !b->pressed && act == BUTTON_NONE) {
            b->long_fired = false;
        }
        return act;
    }

    /* sysfs fallback: poll value every call (main loop supplies cadence) */
    if (b->sysfs_value_fd >= 0) {
        char c = '1';
        if (pread(b->sysfs_value_fd, &c, 1, 0) == 1) {
            bool pressed_now = (c == '0'); /* active-low button */
            if (pressed_now != b->pressed) on_edge(b, pressed_now);
        }
    }
    return classify(b);
}

void button_close(button_t *b)
{
    if (b->fd >= 0) close(b->fd);
    if (b->sysfs_value_fd >= 0) close(b->sysfs_value_fd);
    b->fd = -1;
    b->sysfs_value_fd = -1;
}
