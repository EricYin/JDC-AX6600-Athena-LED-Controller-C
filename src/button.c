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
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/gpio.h>

#define LONG_PRESS_NS   (2LL * 1000000000LL)
#define DOUBLECLICK_NS  (350LL * 1000000LL)

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
    ssize_t n = write(fd, buf, (size_t)len);
    int saved = errno;
    close(fd);
    /* EBUSY: pin already exported (e.g. procd restarted the daemon) - OK. */
    if (n != len && saved != EBUSY) return false;

    char path[96];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", pin);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    bool dir_ok = write(fd, "in", 2) == 2;
    close(fd);
    return dir_ok;
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
                /* Non-blocking: the drain loop below reads until EAGAIN. */
                int flags = fcntl(req.fd, F_GETFL);
                fcntl(req.fd, F_SETFL, flags | O_NONBLOCK);
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

/*
 * Edge-driven gesture state machine.
 * "gesture_done" marks that the current press already produced an action
 * (double click fires on the second press edge, long press fires while
 * held), so the matching release must not be reclassified as SHORT.
 */
static void on_edge(button_t *b, bool pressed_now, button_action_t *out)
{
    long long now = now_ns();

    if (pressed_now) {
        if (b->awaiting_double && now - b->release_ns < DOUBLECLICK_NS) {
            /* second click inside the window: fire DOUBLE immediately */
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
        if (b->gesture_done) return; /* DOUBLE/LONG already delivered */
        b->release_ns = now;
        b->awaiting_double = true;   /* may still become a double click */
    }
}

static button_action_t classify(button_t *b)
{
    long long now = now_ns();

    /* long press fires while still held (no release wait needed) */
    if (b->pressed && !b->gesture_done &&
        now - b->press_start_ns >= LONG_PRESS_NS) {
        b->gesture_done = true;
        return BUTTON_LONG;
    }
    /* double-click window expired -> it was a single short press */
    if (b->awaiting_double && now - b->release_ns >= DOUBLECLICK_NS) {
        b->awaiting_double = false;
        return BUTTON_SHORT;
    }
    return BUTTON_NONE;
}

button_action_t button_poll(button_t *b, int timeout_ms)
{
    button_action_t act = BUTTON_NONE;

    /* cdev: edge events via poll(); zero CPU while the key is idle */
    if (b->fd >= 0) {
        struct pollfd pfd = { .fd = b->fd, .events = POLLIN };
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            struct gpioevent_data ev;
            ssize_t n;
            while ((n = read(b->fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
                if (ev.id == GPIOEVENT_EVENT_FALLING_EDGE)
                    on_edge(b, true, &act);
                else if (ev.id == GPIOEVENT_EVENT_RISING_EDGE)
                    on_edge(b, false, &act);
            }
        }
        if (act == BUTTON_NONE) act = classify(b);
        return act;
    }

    /* sysfs fallback: read level each call; caller supplies cadence */
    if (b->sysfs_value_fd >= 0) {
        char c = '1';
        if (pread(b->sysfs_value_fd, &c, 1, 0) == 1) {
            bool pressed_now = (c == '0'); /* active-low button */
            if (pressed_now != b->pressed) on_edge(b, pressed_now, &act);
        }
    }
    if (act == BUTTON_NONE) act = classify(b);
    return act;
}

void button_close(button_t *b)
{
    if (b->fd >= 0) close(b->fd);
    if (b->sysfs_value_fd >= 0) close(b->sysfs_value_fd);
    b->fd = -1;
    b->sysfs_value_fd = -1;
}
