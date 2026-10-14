#include "monitor.h"
#include "pages.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static char g_ifname[64] = "wan";
static struct timespec g_last_cpu;
static bool g_have_last_cpu;
static struct timespec g_last_net;
static bool g_have_last_net;

static int read_int_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static bool read_cpu(uint64_t *total, uint64_t *idle)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return false;
    uint64_t user=0, nice=0, sys=0, idl=0, iow=0, irq=0, sirq=0, steal=0;
    int n = fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &sys, &idl, &iow, &irq, &sirq, &steal);
    fclose(f);
    if (n < 5) return false;
    *idle = idl + iow;
    *total = user + nice + sys + idl + iow + irq + sirq + steal;
    return true;
}

static bool read_mem(uint64_t *total, uint64_t *available)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return false;
    char key[32];
    uint64_t val;
    *total = *available = 0;
    while (fscanf(f, "%31s %lu kB", key, &val) == 2) {
        if (strcmp(key, "MemTotal:") == 0) *total = val;
        else if (strcmp(key, "MemAvailable:") == 0) *available = val;
        if (*total && *available) break;
    }
    fclose(f);
    return *total > 0;
}

static bool read_net(const char *iface, uint64_t *rx, uint64_t *tx)
{
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return false;
    char line[512];
    *rx = *tx = 0;
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *name = line;
        while (isspace((unsigned char)*name)) name++;
        if (strcmp(name, iface) != 0) continue;
        unsigned long long r=0, t=0;
        if (sscanf(colon + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu",
                   &r, &t) >= 2) {
            *rx = r; *tx = t;
        }
        break;
    }
    fclose(f);
    return true;
}

static double elapsed_sec(const struct timespec *a, const struct timespec *b)
{
    return (double)(b->tv_sec - a->tv_sec) +
           (double)(b->tv_nsec - a->tv_nsec) / 1e9;
}

void monitor_init(monitor_t *m, const char *ifname)
{
    memset(m, 0, sizeof(*m));
    if (ifname && *ifname)
        snprintf(g_ifname, sizeof(g_ifname), "%s", ifname);
    else
        strcpy(g_ifname, "wan");
    m->net_smoothing_secs = 2.0;
    g_have_last_cpu = false;
    g_have_last_net = false;
}

void monitor_set_net_smoothing_seconds(monitor_t *m, double seconds)
{
    if (!m)
        return;
    m->net_smoothing_secs = seconds < 0.0 ? 0.0 : seconds;
}

static void poll_cpu(monitor_t *m)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t total, idle;
    if (read_cpu(&total, &idle)) {
        if (g_have_last_cpu) {
            double dt = elapsed_sec(&g_last_cpu, &now);
            if (dt > 0 && total >= m->cpu_total) {
                uint64_t d_total = total - m->cpu_total;
                uint64_t d_idle  = idle - m->cpu_idle;
                m->cpu_percent = d_total ? 100.0 * (double)(d_total - d_idle) / (double)d_total : 0.0;
            }
        }
        m->cpu_total = total;
        m->cpu_idle = idle;
        g_last_cpu = now;
        g_have_last_cpu = true;
    }
}

static void poll_mem(monitor_t *m)
{
    uint64_t total_kb, avail_kb;
    if (read_mem(&total_kb, &avail_kb)) {
        m->mem_total_kb = total_kb;
        m->mem_available_kb = avail_kb;
        m->mem_percent = total_kb ?
            100.0 * (double)(total_kb - avail_kb) / (double)total_kb : 0.0;
    }
}

static void poll_temp(monitor_t *m)
{
    int cpu_mc = read_int_file("/sys/class/thermal/thermal_zone4/temp");
    int nss_mc = read_int_file("/sys/class/thermal/thermal_zone0/temp");
    if (cpu_mc >= 0) m->temp_cpu_mc = cpu_mc;
    if (nss_mc >= 0) m->temp_nss_mc = nss_mc;
}

static double smooth_rate(double current, double sample, double dt, double seconds)
{
    if (seconds <= 0.0)
        return sample;
    if (dt <= 0.0)
        return current;
    double alpha = 1.0 - exp(-dt / seconds);
    return current + alpha * (sample - current);
}

static void poll_net(monitor_t *m)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t rx, tx;
    if (read_net(g_ifname, &rx, &tx)) {
        double sample_rx_bytes_per_sec = 0.0;
        double sample_tx_bytes_per_sec = 0.0;
        double dt = 0.0;

        if (g_have_last_net) {
            dt = elapsed_sec(&g_last_net, &now);
            if (dt > 0.0) {
                if (rx >= m->rx_bytes)
                    sample_rx_bytes_per_sec = (double)(rx - m->rx_bytes) / dt;
                if (tx >= m->tx_bytes)
                    sample_tx_bytes_per_sec = (double)(tx - m->tx_bytes) / dt;
            }
        }

        if (!m->net_rates_valid) {
            m->rx_bytes_per_sec = sample_rx_bytes_per_sec;
            m->tx_bytes_per_sec = sample_tx_bytes_per_sec;
            m->net_rates_valid = true;
        } else {
            m->rx_bytes_per_sec = smooth_rate(m->rx_bytes_per_sec, sample_rx_bytes_per_sec, dt,
                                    m->net_smoothing_secs);
            m->tx_bytes_per_sec = smooth_rate(m->tx_bytes_per_sec, sample_tx_bytes_per_sec, dt,
                                    m->net_smoothing_secs);
        }

        m->rx_bytes = rx;
        m->tx_bytes = tx;
        g_last_net = now;
        g_have_last_net = true;
    }
}

unsigned monitor_poll_mask_page(int page)
{
    switch ((page_id_t)page) {
    case PAGE_CPU:
        return MONITOR_POLL_CPU;
    case PAGE_MEM:
        return MONITOR_POLL_MEM;
    case PAGE_TEMP:
        return MONITOR_POLL_TEMP;
    case PAGE_NET_DOWN:
    case PAGE_NET_UP:
        return MONITOR_POLL_NET;
    default:
        /* TIME, DATE, LUNAR: no file I/O */
        return 0;
    }
}

void monitor_poll_masked(monitor_t *m, unsigned poll_mask)
{
    if (poll_mask & MONITOR_POLL_CPU)   poll_cpu(m);
    if (poll_mask & MONITOR_POLL_MEM)   poll_mem(m);
    if (poll_mask & MONITOR_POLL_TEMP)  poll_temp(m);
    if (poll_mask & MONITOR_POLL_NET)   poll_net(m);
}

void monitor_poll(monitor_t *m)
{
    poll_cpu(m);
    poll_mem(m);
    poll_temp(m);
    poll_net(m);
}

/*
 * Monitor pages use fixed-width text so rapidly changing values never move
 * the label, digits, decimal point, or unit on the LED panel.
 */
/* ';' renders as a three-column blank digit, so leading zeros can be hidden
 * without moving the remaining digits on the LED panel. */
static void hide_leading_zeroes(char *s, size_t digits)
{
    for (size_t i = 0; i + 1 < digits; i++) {
        if (s[i] != '0')
            break;
        s[i] = ';';
    }
}

static void format_percent(int percent, char *buf, size_t len)
{
    char digits[4];
    if (percent > 999) percent = 999;
    if (percent < 0) percent = 0;
    snprintf(digits, sizeof(digits), "%03d", percent);
    hide_leading_zeroes(digits, 3);
    snprintf(buf, len, "%s", digits);
}

void monitor_format_cpu(const monitor_t *m, char *buf, size_t len)
{
    char percent[4];
    format_percent((int)(m->cpu_percent + 0.5), percent, sizeof(percent));
    snprintf(buf, len, "C^%s%%", percent);
}

void monitor_format_mem(const monitor_t *m, char *buf, size_t len)
{
    char percent[4];
    format_percent((int)(m->mem_percent + 0.5), percent, sizeof(percent));
    snprintf(buf, len, "M^%s%%", percent);
}

void monitor_format_temp(const monitor_t *m, char *buf, size_t len)
{
    int cpu = m->temp_cpu_mc > 0 ? m->temp_cpu_mc / 1000 : 0;
    int nss = m->temp_nss_mc > 0 ? m->temp_nss_mc / 1000 : 0;
    if (cpu > 99) cpu = 99;
    if (nss > 99) nss = 99;
    char cpu_digits[3] = { (cpu < 10) ? ';' : (char)('0' + cpu / 10),
                           (char)('0' + cpu % 10), '\0' };
    char nss_digits[3] = { (nss < 10) ? ';' : (char)('0' + nss / 10),
                           (char)('0' + nss % 10), '\0' };
    snprintf(buf, len, "T^%s.%s", cpu_digits, nss_digits);
}

static void format_bytes_per_sec(double bytes_per_sec, char *buf, size_t len)
{
    static const char units[] = "BKMGT";
    size_t unit = 0;
    double value = bytes_per_sec;

    if (value < 0.0)
        value = 0.0;

    while (unit < sizeof(units) - 2 && value >= 999.95) {
        value /= 1000.0;
        unit++;
    }
    if (value > 999.95)
        value = 999.95;

    snprintf(buf, len, "%05.1f%c", value, units[unit]);
    hide_leading_zeroes(buf, 3);
}

void monitor_format_net(const monitor_t *m, bool down, char *buf, size_t len)
{
    double bytes_per_sec = down ? m->rx_bytes_per_sec : m->tx_bytes_per_sec;
    char rate[16];
    format_bytes_per_sec(bytes_per_sec, rate, sizeof(rate));
    snprintf(buf, len, "%c^%s", down ? 'D' : 'U', rate);
}
