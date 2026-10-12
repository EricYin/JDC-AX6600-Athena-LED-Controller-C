#ifndef ATHENA_LUNAR_H
#define ATHENA_LUNAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int year;
    uint8_t month;
    uint8_t day;
    bool leap_month;
} lunar_date_t;

/* Gregorian -> Chinese lunar. Valid 1900-01-31 .. 2100-12-31. */
bool lunar_from_gregorian(int y, int m, int d, lunar_date_t *out);

/* Screen format: "L:5.7" or leap month "L:R6.15". */
void lunar_format(const lunar_date_t *ld, char *buf, size_t len);

/* Convenience for today (local time). */
void lunar_today(char *buf, size_t len);

#endif
