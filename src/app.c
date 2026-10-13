#include "app.h"

#include "button.h"
#include "font.h"
#include "lunar.h"
#include "monitor.h"
#include "pages.h"
#include "screen.h"
#include "sim.h"

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
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
    } else if (sig == SIGUSR1 || sig == SIGUSR2) {
        button_notify_signal(sig);
    }
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
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
    /* Interrupted immediately when SIGUSR1 or SIGUSR2 arrives */
    nanosleep(&ts, NULL);
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

static uint8_t indicator_status(const monitor_t *mon, const app_args_t *a)
{
    uint8_t s = 0;
    if (mon->cpu_percent > a->indicator_cpu_percent)
        s |= LED_CLOCK_BIT;
    if (mon->rx_bytes_per_sec > a->indicator_net_kbytes * 1000.0)
        s |= LED_DOWN_BIT;
    if (mon->tx_bytes_per_sec > a->indicator_net_kbytes * 1000.0)
        s |= LED_UP_BIT;
    return s;
}

/* Aggregate all pages chosen through --pages/UCI. Metrics for unchecked pages
 * remain completely unpolled; checked metrics keep updating while any other
 * page is displayed so the discrete indicator LEDs never retain stale values. */
static unsigned selected_poll_mask(const app_args_t *a)
{
    unsigned mask = 0;
    for (size_t i = 0; i < a->num_pages; i++)
        mask |= monitor_poll_mask_page(a->pages[i]);
    return mask;
}

static int run_sim(const app_args_t *a)
{
    monitor_t mon;
    monitor_init(&mon, a->net_if);
    monitor_set_net_smoothing_seconds(&mon, a->net_smoothing_secs);

    sim_set_clear(!a->no_clear);
    sim_input_init();
    atexit(sim_input_restore);

    size_t page_idx = 0;
    unsigned page_elapsed_ms = 0;
    bool screen_on = true;
    unsigned poll_mask = selected_poll_mask(a);
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

        /* Keep selected metrics fresh even when another page is displayed,
         * so the discrete CPU and network indicators do not retain stale values. */
        monitor_poll_masked(&mon, poll_mask);

        page_render(cur_page, &mon, text, sizeof(text));
        size_t ncols = font_render(text, cols, COLS_MAX);

        build_frame_centered(cols, ncols, frame);

        uint8_t status = screen_on ? indicator_status(&mon, a) : 0;
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
    button_init_signals();

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

    monitor_t mon;
    monitor_init(&mon, a->net_if);
    monitor_set_net_smoothing_seconds(&mon, a->net_smoothing_secs);

    size_t page_idx = 0;
    unsigned page_elapsed_ms = 0;
    bool screen_on = true;
    unsigned poll_mask = selected_poll_mask(a);
    char text[TEXT_MAX];
    uint8_t cols[COLS_MAX];
    uint8_t frame[FRAME_COLS];

    long long last_tick = monotonic_ms();

    while (g_running) {
        /*
         * Sleep for poll_ms (nanosleep). If SIGUSR1 or SIGUSR2 arrives from procd,
         * nanosleep is interrupted immediately for zero-latency response!
         */
        sleep_ms(a->poll_ms < 50 ? 50 : a->poll_ms);

        button_action_t act = button_get_action();

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

        /* Keep selected metrics fresh even when another page is displayed,
         * so the discrete CPU and network indicators do not retain stale values. */
        monitor_poll_masked(&mon, poll_mask);

        page_render(cur_page, &mon, text, sizeof(text));
        size_t ncols = font_render(text, cols, COLS_MAX);

        build_frame_centered(cols, ncols, frame);

        if (!screen_write_frame(&screen, frame, indicator_status(&mon, a))) {
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

    /* runtime settings and CLI parsing */
    monitor_t settings_test;
    monitor_init(&settings_test, "lo");
    if (settings_test.net_smoothing_secs != 2.0) {
        fprintf(stderr, "FAIL default net smoothing\n");
        failures++;
    }
    monitor_set_net_smoothing_seconds(&settings_test, 0.0);
    if (settings_test.net_smoothing_secs != 0.0) {
        fprintf(stderr, "FAIL disable net smoothing\n");
        failures++;
    }
    monitor_set_net_smoothing_seconds(&settings_test, -1.0);
    if (settings_test.net_smoothing_secs != 0.0) {
        fprintf(stderr, "FAIL clamp net smoothing\n");
        failures++;
    }

    app_args_t args_test;
    char *default_argv[] = { "athena-led-c", NULL };
    if (app_parse_args(&args_test, 1, default_argv) != 0 ||
        args_test.net_smoothing_secs != 2.0) {
        fprintf(stderr, "FAIL default net smoothing arg\n");
        failures++;
    }
    char *custom_argv[] = { "athena-led-c", "--net-smoothing-secs", "4.5", NULL };
    if (app_parse_args(&args_test, 3, custom_argv) != 0 ||
        args_test.net_smoothing_secs != 4.5) {
        fprintf(stderr, "FAIL custom net smoothing arg\n");
        failures++;
    }
    char *negative_argv[] = { "athena-led-c", "--net-smoothing-secs", "-1", NULL };
    if (app_parse_args(&args_test, 3, negative_argv) != 0 ||
        args_test.net_smoothing_secs != 0.0) {
        fprintf(stderr, "FAIL negative net smoothing arg\n");
        failures++;
    }
    if (app_parse_args(&args_test, 1, default_argv) != 0 ||
        args_test.indicator_net_kbytes != 10.0) {
        fprintf(stderr, "FAIL default net threshold arg\n");
        failures++;
    }
    char *threshold_argv[] = { "athena-led-c", "--indicator-net-kbytes", "2.5", NULL };
    if (app_parse_args(&args_test, 3, threshold_argv) != 0 ||
        args_test.indicator_net_kbytes != 2.5) {
        fprintf(stderr, "FAIL custom net threshold arg\n");
        failures++;
    }
    char *negative_threshold_argv[] = { "athena-led-c", "--indicator-net-kbytes", "-1", NULL };
    if (app_parse_args(&args_test, 3, negative_threshold_argv) != 0 ||
        args_test.indicator_net_kbytes != 0.0) {
        fprintf(stderr, "FAIL negative net threshold arg\n");
        failures++;
    }

    /* monitor formatting */
    monitor_t m = {0};
    m.cpu_percent = 12.4; m.mem_percent = 34.5;
    m.temp_cpu_mc = 45123; m.temp_nss_mc = 52123;
    m.rx_bytes_per_sec = 1234567; m.tx_bytes_per_sec = 987654;
    char buf[64];
    monitor_format_cpu(&m, buf, sizeof(buf));
    if (strcmp(buf, "C^;12%") != 0) { fprintf(stderr, "FAIL cpu format: %s\n", buf); failures++; }
    monitor_format_mem(&m, buf, sizeof(buf));
    if (strcmp(buf, "M^;35%") != 0) { fprintf(stderr, "FAIL mem format: %s\n", buf); failures++; }
    monitor_format_temp(&m, buf, sizeof(buf));
    if (strcmp(buf, "T^45.52") != 0) { fprintf(stderr, "FAIL temp format: %s\n", buf); failures++; }
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D^;;1.2M") != 0) { fprintf(stderr, "FAIL net format: %s\n", buf); failures++; }
    m.rx_bytes_per_sec = 999.0;
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D^999.0B") != 0) { fprintf(stderr, "FAIL low-rate net format: %s\n", buf); failures++; }
    m.rx_bytes_per_sec = 123456789.0;
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D^123.5M") != 0) { fprintf(stderr, "FAIL high-rate net format: %s\n", buf); failures++; }
    m.rx_bytes_per_sec = 999.949;
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D^999.9B") != 0) { fprintf(stderr, "FAIL max B format: %s\n", buf); failures++; }
    m.rx_bytes_per_sec = 999.95;
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D^;;1.0K") != 0) { fprintf(stderr, "FAIL net unit rollover: %s\n", buf); failures++; }
    m.rx_bytes_per_sec = 999.95e9;
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D^;;1.0T") != 0) { fprintf(stderr, "FAIL net unit rollover to T: %s\n", buf); failures++; }
    m.rx_bytes_per_sec = 12345.0;
    monitor_format_net(&m, true, buf, sizeof(buf));
    if (strcmp(buf, "D^;12.3K") != 0) { fprintf(stderr, "FAIL net one leading zero: %s\n", buf); failures++; }
    m.cpu_percent = 5.4;
    monitor_format_cpu(&m, buf, sizeof(buf));
    if (strcmp(buf, "C^;;5%") != 0) { fprintf(stderr, "FAIL cpu leading zeros: %s\n", buf); failures++; }
    m.temp_cpu_mc = 5123; m.temp_nss_mc = 5123;
    monitor_format_temp(&m, buf, sizeof(buf));
    if (strcmp(buf, "T^;5.;5") != 0) { fprintf(stderr, "FAIL temp leading zeros: %s\n", buf); failures++; }

    /* Dynamic monitor pages must keep a fixed rendered width. */
    const char *fixed_texts[] = {
        "C^;12%", "M^;35%", "T^45.52",
        "D^;;1.2M", "D^999.0B", "U^123.5M"
    };
    for (size_t i = 0; i < sizeof(fixed_texts) / sizeof(fixed_texts[0]); i++) {
        n = font_render(fixed_texts[i], cols, sizeof(cols));
        if (strcmp(fixed_texts[i], "C^;12%") == 0 && n != 21) {
            fprintf(stderr, "FAIL cpu width: %s=%zu\n", fixed_texts[i], n);
            failures++;
        } else if (strncmp(fixed_texts[i], "D^", 2) == 0 ||
                   strncmp(fixed_texts[i], "U^", 2) == 0) {
            if (n != 27) {
                fprintf(stderr, "FAIL net width: %s=%zu\n", fixed_texts[i], n);
                failures++;
            }
        }
    }

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
