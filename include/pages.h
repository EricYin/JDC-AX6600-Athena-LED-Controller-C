#ifndef ATHENA_PAGES_H
#define ATHENA_PAGES_H

#include "monitor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PAGE_TIME = 0,
    PAGE_DATE,
    PAGE_LUNAR,
    PAGE_SUN,
    PAGE_CPU,
    PAGE_MEM,
    PAGE_TEMP,
    PAGE_NET_DOWN,
    PAGE_NET_UP,
    PAGE_COUNT
} page_id_t;

/* Render page text into buf. Always compact, strictly <= 27 cols. */
void page_render(page_id_t page, const monitor_t *mon,
                 const char *sun_coords, char *buf, size_t len);

const char *page_name(page_id_t page);
page_id_t page_from_name(const char *name);

#endif
