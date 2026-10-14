#include "pages.h"

#include "lunar.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void local_now(struct tm *tm)
{
    time_t t = time(NULL);
    if (!localtime_r(&t, tm))
        memset(tm, 0, sizeof(*tm));
}

void page_render(page_id_t page, const monitor_t *mon, char *buf, size_t len)
{
    struct tm tm;
    local_now(&tm);

    switch (page) {
    case PAGE_TIME: {
        /*
         * Both compact colons stay lit. Exactly 27 columns (single screen):
         * 6 digits x3 + 2 compact colons x1 + 7 gaps = 27 cols.
         */
        snprintf(buf, len, "%02d^%02d^%02d",
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
        break;
    }
    case PAGE_DATE:
        /* Compact date MM-DD (19 cols, centered) */
        snprintf(buf, len, "%02d-%02d", tm.tm_mon + 1, tm.tm_mday);
        break;
    case PAGE_LUNAR:
        /* Lunar date L:M.D (17 cols, centered) */
        lunar_today(buf, len);
        break;
    case PAGE_CPU:
        monitor_format_cpu(mon, buf, len);
        break;
    case PAGE_MEM:
        monitor_format_mem(mon, buf, len);
        break;
    case PAGE_TEMP:
        monitor_format_temp(mon, buf, len);
        break;
    case PAGE_NET_DOWN:
        monitor_format_net(mon, true, buf, len);
        break;
    case PAGE_NET_UP:
        monitor_format_net(mon, false, buf, len);
        break;
    default:
        snprintf(buf, len, "ERR");
        break;
    }
}

const char *page_name(page_id_t page)
{
    static const char *names[PAGE_COUNT] = {
        "time", "net_down", "net_up",
        "mem", "cpu", "temp", "date", "lunar"
    };
    if ((int)page < 0 || page >= PAGE_COUNT) return "?";
    return names[page];
}

page_id_t page_from_name(const char *name)
{
    if (!name || !*name) return PAGE_TIME;
    if (strcmp(name, "time") == 0) return PAGE_TIME;
    if (strcmp(name, "date") == 0) return PAGE_DATE;
    if (strcmp(name, "lunar") == 0) return PAGE_LUNAR;
    if (strcmp(name, "cpu") == 0) return PAGE_CPU;
    if (strcmp(name, "mem") == 0) return PAGE_MEM;
    if (strcmp(name, "temp") == 0) return PAGE_TEMP;
    if (strcmp(name, "net_down") == 0 || strcmp(name, "net-down") == 0) return PAGE_NET_DOWN;
    if (strcmp(name, "net_up") == 0 || strcmp(name, "net-up") == 0) return PAGE_NET_UP;
    return PAGE_TIME;
}
