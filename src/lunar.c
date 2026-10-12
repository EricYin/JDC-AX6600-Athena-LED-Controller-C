/*
 * Local Chinese lunar calendar conversion, classic lookup-table algorithm.
 * Data covers 1900..2100 and needs no network, no zoneinfo, no allocations.
 */
#include "lunar.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const uint32_t LUNAR_INFO[201] = {
    0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2,
    0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977,
    0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970,
    0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950,
    0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557,
    0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0,
    0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0,
    0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6,
    0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570,
    0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x055c0,0x0ab60,0x096d5,0x092e0,
    0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5,
    0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930,
    0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530,
    0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45,
    0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0,
    0x14b63,0x09370,0x049f8,0x04970,0x064b0,0x168a6,0x0ea50,0x06b20,0x1a6c4,0x0aae0,
    0x0a2e0,0x0d2e3,0x0c960,0x0d557,0x0d4a0,0x0da50,0x05d55,0x056a0,0x0a6d0,0x055d4,
    0x052d0,0x0a9b8,0x0a950,0x0b4a0,0x0b6a6,0x0ad50,0x055a0,0x0aba4,0x0a5b0,0x052b0,
    0x0b273,0x06930,0x07337,0x06aa0,0x0ad50,0x14b55,0x04b60,0x0a570,0x054e4,0x0d160,
    0x0e968,0x0d520,0x0daa0,0x16aa6,0x056d0,0x04ae0,0x0a9d4,0x0a2d0,0x0d150,0x0f252,
    0x0d520
};

static uint32_t leap_month(int year)
{
    return LUNAR_INFO[year - 1900] & 0xf;
}

static uint32_t leap_days(int year)
{
    if (leap_month(year) != 0)
        return (LUNAR_INFO[year - 1900] & 0x10000) ? 30 : 29;
    return 0;
}

static uint32_t month_days(int year, int m)
{
    return (LUNAR_INFO[year - 1900] & (0x10000 >> m)) ? 30 : 29;
}

static uint32_t year_days(int year)
{
    uint32_t sum = 348;
    uint32_t info = LUNAR_INFO[year - 1900];
    for (uint32_t mask = 0x8000; mask > 0x8; mask >>= 1)
        if (info & mask) sum++;
    return sum + leap_days(year);
}

/* Days since 1970-01-01 (Gregorian), no leap-second / DST complications. */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

bool lunar_from_gregorian(int y, int m, int d, lunar_date_t *out)
{
    if (y < 1900 || y > 2100 || !out) return false;

    long offset = days_from_civil(y, m, d) - days_from_civil(1900, 1, 31);
    if (offset < 0) return false;

    int year = 1900;
    while (year <= 2100) {
        uint32_t days = year_days(year);
        if ((uint32_t)offset < days) break;
        offset -= days;
        year++;
    }
    if (year > 2100) return false;

    uint32_t leap = leap_month(year);
    bool is_leap = false;
    uint32_t month = 1;
    for (;;) {
        uint32_t days = is_leap ? leap_days(year) : month_days(year, (int)month);
        if ((uint32_t)offset < days) break;
        offset -= days;

        if (is_leap) {
            is_leap = false;
            month++;
        } else if (leap != 0 && month == leap) {
            is_leap = true;
        } else {
            month++;
        }
        if (month > 12) return false;
    }

    out->year = year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)(offset + 1);
    out->leap_month = is_leap;
    return true;
}

void lunar_format(const lunar_date_t *ld, char *buf, size_t len)
{
    if (!ld || !buf || len == 0) return;
    if (ld->leap_month)
        snprintf(buf, len, "L:R%u.%u", ld->month, ld->day);
    else
        snprintf(buf, len, "L:%u.%u", ld->month, ld->day);
}

void lunar_today(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm tm;
    if (!localtime_r(&now, &tm)) { snprintf(buf, len, "L:--"); return; }
    lunar_date_t ld;
    if (lunar_from_gregorian(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, &ld))
        lunar_format(&ld, buf, len);
    else
        snprintf(buf, len, "L:--");
}
