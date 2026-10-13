#include "monitor.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static char g_ifname[64] = "br-lan";
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
        strcpy(g_ifname, "br-lan");
    g_have_last_cpu = false;
    g_have_last_net = false;
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

static void poll_net(monitor_t *m)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t rx, tx;
    if (read_net(g_ifname, &rx, &tx)) {
        if (g_have_last_net) {
            double dt = elapsed_sec(&g_last_net, &now);
            if (dt > 0 && rx >= m->rx_bytes) {
                m->rx_bps = (double)(rx - m->rx_bytes) * 8.0 / dt;
                m->tx_bps = (double)(tx - m->tx_bytes) * 8.0 / dt;
            }
        }
        m->rx_bytes = rx;
        m->tx_bytes = tx;
        g_last_net = now;
        g_have_last_net = true;
    }
}

void monitor_poll_page(monitor_t *m, int page)
{
    /* PAGE_CPU=3, PAGE_MEM=4, PAGE_TEMP=5, PAGE_NET_DOWN=6, PAGE_NET_UP=7 */
    switch (page) {
    case 3: /* PAGE_CPU */
        poll_cpu(m);
        break;
    case 4: /* PAGE_MEM */
        poll_mem(m);
        break;
    case 5: /* PAGE_TEMP */
        poll_temp(m);
        break;
    case 6: /* PAGE_NET_DOWN */
    case 7: /* PAGE_NET_UP */
        poll_net(m);
        break;
    default:
        /* TIME (0), DATE (1), LUNAR (2): ZERO file I/O! */
        break;
    }
}

void monitor_poll(monitor_t *m)
{
    poll_cpu(m);
    poll_mem(m);
    poll_temp(m);
    poll_net(m);
}

void monitor_format_cpu(const monitor_t *m, char *buf, size_t len)
{
    snprintf(buf, len, "C:%d%%", (int)(m->cpu_percent + 0.5));
}

void monitor_format_mem(const monitor_t *m, char *buf, size_t len)
{
    snprintf(buf, len, "M:%d%%", (int)(m->mem_percent + 0.5));
}

void monitor_format_temp(const monitor_t *m, char *buf, size_t len)
{
    int cpu = m->temp_cpu_mc > 0 ? m->temp_cpu_mc / 1000 : 0;
    int nss = m->temp_nss_mc > 0 ? m->temp_nss_mc / 1000 : 0;
    snprintf(buf, len, "T:%d.%d", cpu, nss);
}

static void format_bits(double bits, char *buf, size_t len)
{
    if (bits >= 1e9) snprintf(buf, len, "%.1fG", bits / 1e9);
    else if (bits >= 1e6) snprintf(buf, len, "%.1fM", bits / 1e6);
    else if (bits >= 1e3) snprintf(buf, len, "%.0fK", bits / 1e3);
    else snprintf(buf, len, "%.0f", bits);
}

void monitor_format_net(const monitor_t *m, bool down, char *buf, size_t len)
{
    double bps = down ? m->rx_bps : m->tx_bps;
    char rate[16];
    format_bits(bps, rate, sizeof(rate));
    snprintf(buf, len, "%c:%s", down ? 'D' : 'U', rate);
}
