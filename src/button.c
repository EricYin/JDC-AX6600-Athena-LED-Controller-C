#include "button.h"

#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define DOUBLECLICK_NS (350LL * 1000000LL) /* 350 ms window */

static volatile sig_atomic_t g_pending_short = 0;
static volatile sig_atomic_t g_pending_double = 0;
static volatile sig_atomic_t g_pending_long = 0;
static long long g_last_click_ns = 0;

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void button_init_signals(void)
{
    g_pending_short = 0;
    g_pending_double = 0;
    g_pending_long = 0;
    g_last_click_ns = 0;
}

void button_notify_signal(int sig)
{
    long long now = now_ns();

    if (sig == SIGUSR1) {
        /* If second click arrives within double-click window -> DOUBLE */
        if (g_pending_short && (now - g_last_click_ns < DOUBLECLICK_NS)) {
            g_pending_short = 0;
            g_pending_double = 1;
            g_last_click_ns = 0;
        } else {
            g_pending_short = 1;
            g_last_click_ns = now;
        }
    } else if (sig == SIGUSR2) {
        g_pending_short = 0;
        g_pending_double = 0;
        g_last_click_ns = 0;
        g_pending_long = 1;
    }
}

button_action_t button_get_action(void)
{
    if (g_pending_double) {
        g_pending_double = 0;
        return BUTTON_DOUBLE;
    }
    if (g_pending_long) {
        g_pending_long = 0;
        return BUTTON_LONG;
    }
    if (g_pending_short) {
        long long now = now_ns();
        if (now - g_last_click_ns >= DOUBLECLICK_NS) {
            g_pending_short = 0;
            return BUTTON_SHORT;
        }
    }
    return BUTTON_NONE;
}
