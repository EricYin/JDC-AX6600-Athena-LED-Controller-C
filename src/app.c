#include "app.h"

#include "button.h"
#include "font.h"
#include "lunar.h"
#include "monitor.h"
#include "pages.h"
#include "screen.h"
#include "sim.h"
#include "sun.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEXT_MAX 64
#define COLS_MAX 256
#define FRAME_COLS 27

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static long long monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void sleep_ms(unsigned ms)
{
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* retry remaining time; never busy-wait */
    }
}

static void build_frame_centered(const uint8_t *cols, size_t n, uint8_t out[FRAME_COLS])
{
    memset(out, 0, FRAME_COLS);
    if (n >= FRAME_COLS) {
        memcpy(out, cols, FRAME_COLS);
    } else if (n > 0) {
        size_t off = (FRAME_COLS - n) / 2;
        memcpy(out + off, cols, n);
    }
}

static uint8_t indicator_status(const monitor_t *mon)
{
    uint8_t s = 0;
    if (mon->cpu_percent > 5.0) s |= LED_CLOCK_BIT;
    if (mon->rx_bps > 1.0) s |= LED_DOWN_BIT;
    if (mon->tx_bps > 1.0) s |= LED_UP_BIT;
    return s;
}

static int run_sim(const app_args_t *a)
{
    monitor_t mon;
    monitor_init(&mon, a->net_if);

    sim_set_clear(!a->no_clear);
    sim_input_init();
    atexit(sim_input_restore);

    size_t page_idx = 0;
    unsigned page_elapsed_ms = 0;
    bool screen_on = true;
    char text[TEXT_MAX];
    uint8_t cols[COLS_MAX];
    uint8_t frame[FRAME_COLS];

    printf("Athena LED simulator — 27x5 matrix, pages=%zu, auto_flip=%us, poll=%ums\n",
           a->num_pages, a->page_secs, a->poll_ms);

    for (;;) {
        char c = sim_input_read();
        if (c == 'q') break;
        if (c == 'n') { page_idx = (page_idx + 1) % a->num_pages; page_elapsed_ms = 0; }
        if (c == 'p') { page_idx = (page_idx + a->num_pages - 1) % a->num_pages; page_elapsed_ms = 0; }
        if (c == ' ') screen_on = !screen_on;

        page_id_t cur_page = a->pages[page_idx];

        /* Lazy polling: poll ONLY when the current page actually needs the metric */
        monitor_poll_page(&mon, cur_page);

        page_render(cur_page, &mon, a->sun_coords, text, sizeof(text));
        size_t ncols = font_render(text, cols, COLS_MAX);

        build_frame_centered(cols, ncols, frame);

        uint8_t status = screen_on ? indicator_status(&mon) : 0;
        if (screen_on)
            sim_print_frame(frame, status);
        else
            sim_print_frame((const uint8_t[FRAME_COLS]){0}, 0);

        sleep_ms(a->poll_ms);
        page_elapsed_ms += a->poll_ms;

        /* Auto flip only if page_secs > 0 */
        if (a->page_secs > 0 && page_elapsed_ms >= a->page_secs * 1000u) {
            page_idx = (page_idx + 1) % a->num_pages;
            page_elapsed_ms = 0;
        }
    }

    sim_input_restore();
    return 0;
}

static int run_hw(const app_args_t *a)
{
    install_signal_handlers();

    screen_t screen;
    if (!screen_init(&screen, GPIO_BACKEND_AUTO, 0)) {
        fprintf(stderr, "failed to init LED GPIO (cdev/sysfs)\n");
        return 1;
    }
    if (!screen_power(&screen, true, a->brightness)) {
        fprintf(stderr, "failed to power LED panel\n");
        screen_close(&screen);
        return 1;
    }

    button_t button;
    bool button_ok = button_init(&button, BUTTON_GPIO_PIN);
    if (!button_ok)
        fprintf(stderr, "warning: screen button unavailable\n");

    monitor_t mon;
    monitor_init(&mon, a->net_if);

    size_t page_idx = 0;
    unsigned page_elapsed_ms = 0;
    bool screen_on = true;
    char text[TEXT_MAX];
    uint8_t cols[COLS_MAX];
    uint8_t frame[FRAME_COLS];

    long long last_tick = monotonic_ms();

    while (g_running) {
        /*
         * button_poll() blocks for poll_ms with zero CPU when idle,
         * waking immediately on a key edge so page switches feel instant.
         */
        button_action_t act = BUTTON_NONE;
        if (button_ok)
            act = button_poll(&button, (int)a->poll_ms);
        else
            sleep_ms(a->poll_ms);

        long long now = monotonic_ms();
        unsigned elapsed = (unsigned)(now - last_tick);
        last_tick = now;
        page_elapsed_ms += elapsed;

        switch (act) {
        case BUTTON_SHORT:
            page_idx = (page_idx + 1) % a->num_pages;
            page_elapsed_ms = 0;
            fprintf(stderr, "[button] SHORT click -> switch to page %s (%zu/%zu)\n",
                    page_name(a->pages[page_idx]), page_idx + 1, a->num_pages);
            break;
        case BUTTON_DOUBLE:
            page_idx = 0;
            page_elapsed_ms = 0;
            fprintf(stderr, "[button] DOUBLE click -> reset to page %s\n",
                    page_name(a->pages[page_idx]));
            break;
        case BUTTON_LONG:
            screen_on = !screen_on;
            screen_power(&screen, screen_on,
                         screen_on ? a->brightness : 0);
            if (!screen_on) {
                uint8_t blank[FRAME_COLS] = {0};
                screen_write_frame(&screen, blank, 0);
            }
            fprintf(stderr, "[button] LONG press -> screen power %s\n",
                    screen_on ? "ON" : "OFF");
            break;
        default:
            break;
        }
        if (!screen_on) continue;

        page_id_t cur_page = a->pages[page_idx];

        /*
         * Lazy polling: poll ONLY when the current page needs the metric.
         * If cur_page is TIME, DATE, LUNAR, or SUN, performs ZERO file reads!
         */
        monitor_poll_page(&mon, cur_page);

        page_render(cur_page, &mon, a->sun_coords, text, sizeof(text));
        size_t ncols = font_render(text, cols, COLS_MAX);

        /* Single screen, zero horizontal scrolling */
        build_frame_centered(cols, ncols, frame);

        if (!screen_write_frame(&screen, frame, indicator_status(&mon))) {
            fprintf(stderr, "failed to write LED frame\n");
            screen_close(&screen);
            return 1;
        }

        /* Auto flip ONLY when page_secs > 0 */
        if (a->page_secs > 0 && page_elapsed_ms >= a->page_secs * 1000u) {
            page_idx = (page_idx + 1) % a->num_pages;
            page_elapsed_ms = 0;
        }
    }

    /* graceful shutdown */
    screen_power(&screen, false, 0);
    if (button_ok) button_close(&button);
    screen_close(&screen);
    return 0;
}

int app_run(const app_args_t *a)
{
    return a->sim ? run_sim(a) : run_hw(a);
}

int app_self_test(void)
{
    int failures = 0;

    /* font sanity */
    uint8_t cols[64];
    size_t n = font_render("12:34", cols, sizeof(cols));
    if (n == 0 || n > sizeof(cols)) {
        fprintf(stderr, "FAIL font render\n");
        failures++;
    }

    /* lunar anchors */
    lunar_date_t ld;
    if (!lunar_from_gregorian(2025, 1, 29, &ld) ||
        ld.year != 2025 || ld.month != 1 || ld.day != 1 || ld.leap_month) {
        fprintf(stderr, "FAIL lunar spring festival 2025\n");
        failures++;
    }

    /* sunrise/sunset */
    double rise, set;
    if (!sun_calc(39.90, 116.40, 172, 8.0, &rise, &set)) {
        fprintf(stderr, "FAIL sun polar miscalc\n");
        failures++;
    }

    /* monitor formatting */
    monitor_t m = {0};
    m.cpu_percent = 12.4; m.mem_percent = 34.5;
    m.temp_cpu_mc = 45123; m.temp_nss_mc = 52123;
    m.rx_bps = 1234567; m.tx_bps = 987654;
    char buf[64];
    monitor_format_cpu(&m, buf, sizeof(buf));
    if (strcmp(buf, "C:12%") != 0) { fprintf(stderr, "FAIL cpu format: %s\n", buf); failures++; }
    monitor_format_mem(&m, buf, sizeof(buf));
    if (strcmp(buf, "M:35%") != 0) { fprintf(stderr, "FAIL mem format: %s\n", buf); failures++; }
    monitor_format_temp(&m, buf, sizeof(buf));
    if (strcmp(buf, "T:45.52") != 0) { fprintf(stderr, "FAIL temp format: %s\n", buf); failures++; }
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D:1.2M") != 0) { fprintf(stderr, "FAIL net format: %s\n", buf); failures++; }

    /* time page must be exactly 27 columns (single screen) */
    n = font_render("12^34^56", cols, sizeof(cols));
    if (n != 27) { fprintf(stderr, "FAIL time width on=%zu\n", n); failures++; }

    /* page_from_name tests */
    if (page_from_name("time") != PAGE_TIME ||
        page_from_name("temp") != PAGE_TEMP ||
        page_from_name("net_down") != PAGE_NET_DOWN) {
        fprintf(stderr, "FAIL page_from_name\n");
        failures++;
    }

    printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
