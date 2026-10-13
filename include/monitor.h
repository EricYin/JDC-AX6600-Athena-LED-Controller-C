#ifndef ATHENA_MONITOR_H
#define ATHENA_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    /* CPU */
    uint64_t cpu_total, cpu_idle;
    double cpu_percent;
    /* Memory */
    uint64_t mem_total_kb, mem_available_kb;
    double mem_percent;
    /* Temperature (millidegrees C, -1 = unavailable) */
    int temp_cpu_mc;
    int temp_nss_mc;
    /* Network counters for the configured interface */
    uint64_t rx_bytes, tx_bytes;
    double rx_bps, tx_bps;
} monitor_t;

void monitor_init(monitor_t *m, const char *ifname);

/* Lazy polling: poll ONLY the metric required by the current active page.
 * If page is TIME, DATE, LUNAR, or SUN, performs ZERO file I/O or syscalls. */
void monitor_poll_page(monitor_t *m, int page);

/* Full poll (used for tests/diagnostics only) */
void monitor_poll(monitor_t *m);

/* Format helpers writing into caller-provided buffers. */
void monitor_format_cpu(const monitor_t *m, char *buf, size_t len);
void monitor_format_mem(const monitor_t *m, char *buf, size_t len);
void monitor_format_temp(const monitor_t *m, char *buf, size_t len);
void monitor_format_net(const monitor_t *m, bool down, char *buf, size_t len);

#endif
