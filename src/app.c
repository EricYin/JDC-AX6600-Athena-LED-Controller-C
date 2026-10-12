#include "app.h"

#include "font.h"
#include "lunar.h"
#include "monitor.h"
#include "pages.h"
#include "screen.h"
#include "sim.h"
#include "sun.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEXT_MAX 64
#define COLS_MAX 256
#define FRAME_COLS 27

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
    /* medal LED reserved for local connectivity state; off in v0.1 */
    return s;
}

static int run_sim(const app_args_t *a)
{
    monitor_t mon;
    monitor_init(&mon, a->net_if);

    sim_set_clear(!a->no_clear);
    sim_input_init();
    atexit(sim_input_restore);

    page_id_t page = PAGE_TIME;
    unsigned page_elapsed_ms = 0;
    unsigned scroll_pos = 0;
    char text[TEXT_MAX];
    uint8_t cols[COLS_MAX];
    size_t ncols = 0;

    printf("Athena LED simulator — 27x5 matrix, page=%us, poll=%ums, scroll=%ums\n",
           a->page_secs, a->poll_ms, a->scroll_ms);

    for (;;) {
        char c = sim_input_read();
        if (c == 'q') break;
        if (c == 'n') { page = (page + 1) % PAGE_COUNT; page_elapsed_ms = 0; scroll_pos = 0; }
        if (c == 'p') { page = (page + PAGE_COUNT - 1) % PAGE_COUNT; page_elapsed_ms = 0; scroll_pos = 0; }

        monitor_poll(&mon);
        page_render(page, &mon, a->sun_coords, text, sizeof(text));
        ncols = font_render(text, cols, COLS_MAX);

        uint8_t frame[FRAME_COLS];
        if (ncols > FRAME_COLS) {
            /* smooth scrolling window */
            unsigned windows = (unsigned)(ncols - FRAME_COLS) + 1;
            size_t off = scroll_pos % windows;
            memcpy(frame, cols + off, FRAME_COLS);
            scroll_pos = (scroll_pos + 1) % windows;
            /* brief pause at each end for readability */
            if (off == 0 || off == windows - 1) sleep_ms(600);
        } else {
            build_frame_centered(cols, ncols, frame);
        }

        uint8_t status = indicator_status(&mon);
        sim_print_frame(frame, status);

        sleep_ms(a->poll_ms);
        page_elapsed_ms += a->poll_ms;
        if (page_elapsed_ms >= a->page_secs * 1000u) {
            page = (page + 1) % PAGE_COUNT;
            page_elapsed_ms = 0;
            scroll_pos = 0;
        }
    }

    sim_input_restore();
    return 0;
}

static int run_hw(const app_args_t *a)
{
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

    page_id_t page = PAGE_TIME;
    unsigned page_elapsed_ms = 0;
    unsigned scroll_pos = 0;
    char text[TEXT_MAX];
    uint8_t cols[COLS_MAX];
    uint8_t frame[FRAME_COLS];

    for (;;) {
        monitor_poll(&mon);
        page_render(page, &mon, a->sun_coords, text, sizeof(text));
        size_t ncols = font_render(text, cols, COLS_MAX);

        if (ncols > FRAME_COLS) {
            unsigned windows = (unsigned)(ncols - FRAME_COLS) + 1;
            size_t off = scroll_pos % windows;
            memcpy(frame, cols + off, FRAME_COLS);
            scroll_pos = (scroll_pos + 1) % windows;
        } else {
            build_frame_centered(cols, ncols, frame);
        }

        if (!screen_write_frame(&screen, frame, indicator_status(&mon))) {
            fprintf(stderr, "failed to write LED frame\n");
            screen_close(&screen);
            return 1;
        }

        sleep_ms(a->poll_ms);
        page_elapsed_ms += a->poll_ms;
        if (page_elapsed_ms >= a->page_secs * 1000u) {
            page = (page + 1) % PAGE_COUNT;
            page_elapsed_ms = 0;
            scroll_pos = 0;
        }
    }
    /* not reached in v0.1; SIGTERM handling comes with the daemon phase */
}

int app_run(const app_args_t *a)
{
    return a->sim ? run_sim(a) : run_hw(a);
}

int app_self_test(void)
{
    int failures = 0;

    /* font sanity */
    uint8_t cols[16];
    size_t n = font_render("12:34", cols, sizeof(cols));
    if (n == 0 || n > sizeof(cols)) {
        fprintf(stderr, "FAIL font render\n");
        failures++;
    }

    /* lunar anchors from public calendar data */
    lunar_date_t ld;
    if (!lunar_from_gregorian(2025, 1, 29, &ld) ||
        ld.year != 2025 || ld.month != 1 || ld.day != 1 || ld.leap_month) {
        fprintf(stderr, "FAIL lunar spring festival 2025\n");
        failures++;
    }
    if (!lunar_from_gregorian(2025, 7, 25, &ld) ||
        ld.month != 6 || ld.day != 1 || !ld.leap_month) {
        fprintf(stderr, "FAIL lunar leap month 2025\n");
        failures++;
    }
    if (!lunar_from_gregorian(2025, 10, 6, &ld) ||
        ld.month != 8 || ld.day != 15) {
        fprintf(stderr, "FAIL lunar mid-autumn 2025\n");
        failures++;
    }

    /* sunrise/sunset: Beijing 2025-06-21 (doy 172), tolerance ±15 minutes */
    double rise, set;
    if (!sun_calc(39.90, 116.40, 172, 8.0, &rise, &set)) {
        fprintf(stderr, "FAIL sun polar miscalc\n");
        failures++;
    } else {
        if (rise * 60 < 4 * 60 + 31 || rise * 60 > 5 * 60 + 1) {
            fprintf(stderr, "FAIL sun rise %.2f\n", rise);
            failures++;
        }
        if (set * 60 < 19 * 60 + 31 || set * 60 > 20 * 60 + 1) {
            fprintf(stderr, "FAIL sun set %.2f\n", set);
            failures++;
        }
    }
    if (sun_calc(78.0, 15.0, 355, 1.0, &rise, &set)) {
        fprintf(stderr, "FAIL polar night should be unsupported\n");
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

    printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
