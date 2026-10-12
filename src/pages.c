/* glibc exposes tm_gmtoff only with _GNU_SOURCE / _DEFAULT_SOURCE. */
#define _GNU_SOURCE
#include "pages.h"

#include "lunar.h"
#include "sun.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void local_now(struct tm *tm)
{
    time_t t = time(NULL);
    if (!localtime_r(&t, tm))
        memset(tm, 0, sizeof(*tm));
}

static int day_of_year(const struct tm *tm)
{
    return tm->tm_yday + 1;
}

static double local_tz_hours(void)
{
    time_t t = time(NULL);
    struct tm local;
    if (!localtime_r(&t, &local)) return 8.0;
    return (double)(local.tm_gmtoff) / 3600.0;
}

/* Default location: Beijing. Can be overridden by --sun-coords "lat,lon". */
static bool parse_coords(const char *s, double *lat, double *lon)
{
    if (!s || !*s) { *lat = 39.90; *lon = 116.40; return true; }
    double a, b;
    if (sscanf(s, "%lf,%lf", &a, &b) != 2) { *lat = 39.90; *lon = 116.40; return false; }
    if (a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0) return false;
    *lat = a; *lon = b;
    return true;
}

void page_render(page_id_t page, const monitor_t *mon,
                 const char *sun_coords, char *buf, size_t len)
{
    struct tm tm;
    local_now(&tm);

    switch (page) {
    case PAGE_TIME: {
        /*
         * Blinking colon, exactly 27 columns to fill the screen without
         * scrolling: 6 digits x3 + 2 compact colons x1 + 7 gaps = 27.
         * '^' is the 1-column compact colon; ' ' is the blank 1-column
         * colon used for the blink-off phase.
         */
        char colon = (tm.tm_sec % 2 == 0) ? '^' : ' ';
        snprintf(buf, len, "%02d%c%02d^%02d",
                 tm.tm_hour, colon, tm.tm_min, tm.tm_sec);
        break;
    }
    case PAGE_DATE:
        snprintf(buf, len, "%04d-%02d-%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        break;
    case PAGE_LUNAR:
        lunar_today(buf, len);
        break;
    case PAGE_SUN: {
        double lat, lon;
        parse_coords(sun_coords, &lat, &lon);
        sun_format(lat, lon, day_of_year(&tm), local_tz_hours(), buf, len);
        break;
    }
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
        "time", "date", "lunar", "sun",
        "cpu", "mem", "temp", "net-down", "net-up"
    };
    if ((int)page < 0 || page >= PAGE_COUNT) return "?";
    return names[page];
}
