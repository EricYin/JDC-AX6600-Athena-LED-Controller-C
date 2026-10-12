#ifndef ATHENA_SUN_H
#define ATHENA_SUN_H

#include <stdbool.h>
#include <stddef.h>

/* NOAA sunrise/sunset calculation, fully local. */
bool sun_calc(double lat, double lon, int day_of_year, double tz_hours,
              double *rise_out, double *set_out); /* hours 0..24 */

/* Screen format "6:02~19:23", or "SUN:--" for polar day/night. */
void sun_format(double lat, double lon, int day_of_year, double tz_hours,
                char *buf, size_t len);

#endif
