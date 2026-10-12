#include "sun.h"

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define D2R (M_PI / 180.0)

static double calc_event_utc(double doy, double lat, double lon, bool rising)
{
    const double zenith = 90.833;
    double lng_hour = lon / 15.0;
    double t = rising ? doy + ((6.0 - lng_hour) / 24.0)
                      : doy + ((18.0 - lng_hour) / 24.0);

    double m = 0.9856 * t - 3.289;
    double l = m + 1.916 * sin(m * D2R) + 0.020 * sin(2 * m * D2R) + 282.634;
    l = fmod(fmod(l, 360.0) + 360.0, 360.0);

    double ra = atan(0.91764 * tan(l * D2R)) / D2R;
    ra = fmod(fmod(ra, 360.0) + 360.0, 360.0);
    double lq = floor(l / 90.0) * 90.0;
    double rq = floor(ra / 90.0) * 90.0;
    ra = (ra + (lq - rq)) / 15.0;

    double sin_dec = 0.39782 * sin(l * D2R);
    double cos_dec = cos(asin(sin_dec));

    double cos_h = (cos(zenith * D2R) - sin_dec * sin(lat * D2R)) /
                   (cos_dec * cos(lat * D2R));
    if (cos_h > 1.0 || cos_h < -1.0)
        return -1.0;

    double h = rising ? 360.0 - acos(cos_h) / D2R : acos(cos_h) / D2R;
    h /= 15.0;

    double t_local = h + ra - 0.06571 * t - 6.622;
    double utc = t_local - lng_hour;
    utc = fmod(fmod(utc, 24.0) + 24.0, 24.0);
    return utc;
}

bool sun_calc(double lat, double lon, int day_of_year, double tz_hours,
              double *rise_out, double *set_out)
{
    double rise = calc_event_utc((double)day_of_year, lat, lon, true);
    double set = calc_event_utc((double)day_of_year, lat, lon, false);
    if (rise < 0 || set < 0) return false;
    *rise_out = fmod(rise + tz_hours + 24.0, 24.0);
    *set_out  = fmod(set + tz_hours + 24.0, 24.0);
    return true;
}

static void fmt_hour(double hours, char *buf, size_t len)
{
    long total = (long)(hours * 60.0 + 0.5) % (24 * 60);
    snprintf(buf, len, "%ld:%02ld", total / 60, total % 60);
}

void sun_format(double lat, double lon, int day_of_year, double tz_hours,
                char *buf, size_t len)
{
    double rise, set;
    if (!sun_calc(lat, lon, day_of_year, tz_hours, &rise, &set)) {
        snprintf(buf, len, "SUN:--");
        return;
    }
    char r[8], s[8];
    fmt_hour(rise, r, sizeof(r));
    fmt_hour(set, s, sizeof(s));
    snprintf(buf, len, "%s~%s", r, s);
}
