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
    /* Network counters for the configured interface.
     * rx/tx_bytes_per_sec are EMA-smoothed byte/s rates. */
    uint64_t rx_bytes, tx_bytes;
    double rx_bytes_per_sec, tx_bytes_per_sec;
    double net_smoothing_secs;
    bool net_rates_valid;
} monitor_t;

void monitor_init(monitor_t *m, const char *ifname);
void monitor_set_net_smoothing_seconds(monitor_t *m, double seconds);

/* Poll only the metrics selected by `poll_mask`. Used by the runtime loop
 * so background indicator sampling exactly follows the configured pages. */
#define MONITOR_POLL_CPU  (1u << 0)
#define MONITOR_POLL_MEM  (1u << 1)
#define MONITOR_POLL_TEMP (1u << 2)
#define MONITOR_POLL_NET  (1u << 3)
#define MONITOR_POLL_ALL  0x0fu

unsigned monitor_poll_mask_page(int page);
void monitor_poll_masked(monitor_t *m, unsigned poll_mask);

/* Compatibility helper: poll the metric required by one page. */
void monitor_poll_page(monitor_t *m, int page);

/* Full poll (used for tests/diagnostics only) */
void monitor_poll(monitor_t *m);

/* Format helpers writing into caller-provided buffers. */
void monitor_format_cpu(const monitor_t *m, char *buf, size_t len);
void monitor_format_mem(const monitor_t *m, char *buf, size_t len);
void monitor_format_temp(const monitor_t *m, char *buf, size_t len);
void monitor_format_net(const monitor_t *m, bool down, char *buf, size_t len);

#endif
