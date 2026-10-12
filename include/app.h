#ifndef ATHENA_APP_H
#define ATHENA_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool sim;               /* --sim / --test */
    bool no_clear;          /* --no-clear */
    bool self_test;         /* --self-test */
    unsigned page_secs;     /* --page-secs, default 5 */
    unsigned scroll_ms;     /* --scroll-ms, default 128 */
    unsigned poll_ms;       /* --poll-ms, default 250 */
    uint8_t brightness;     /* --brightness, 0..7 */
    const char *net_if;     /* --net-interface */
    const char *sun_coords; /* --sun-coords "lat,lon" */
} app_args_t;

int app_parse_args(app_args_t *a, int argc, char **argv);
int app_run(const app_args_t *a);

/* Exposed for tests. */
int app_self_test(void);

#endif
