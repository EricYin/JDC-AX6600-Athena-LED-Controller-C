#ifndef ATHENA_APP_H
#define ATHENA_APP_H

#include "pages.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool sim;               /* --sim / --test */
    bool no_clear;          /* --no-clear */
    bool self_test;         /* --self-test */
    unsigned page_secs;     /* --page-secs, 0 = manual only; >0 = auto rotation seconds */
    unsigned poll_ms;       /* --poll-ms, default 250 */
    uint8_t brightness;     /* --brightness, 0..7 */
    const char *net_if;     /* --net-interface */
    const char *sun_coords; /* --sun-coords "lat,lon" */
    page_id_t pages[PAGE_COUNT];
    size_t num_pages;
} app_args_t;

int app_parse_args(app_args_t *a, int argc, char **argv);
int app_run(const app_args_t *a);

/* Exposed for tests. */
int app_self_test(void);

#endif
