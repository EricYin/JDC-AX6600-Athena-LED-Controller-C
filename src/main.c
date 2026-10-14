#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out)
{
    fputs(
"athena-led-c — JDCloud AX6600 Athena front LED matrix controller\n"
"\n"
"Usage: athena-led-c [OPTIONS]\n"
"\n"
"Options:\n"
"  --sim, --test             run terminal simulator (27x5, ■/·)\n"
"  --no-clear                simulator: do not clear terminal each frame\n"
"  --screen-on 0|1           initial screen power state (default 1: ON)\n"
"  --custom-text TEXT        custom text for text page (max 27 cols, default ATHENA)\n"
"  --pages LIST              comma/space separated pages to display:\n"
"                            time,net_down,net_up,mem,cpu,temp,date,lunar,text\n"
"  --page NAME               add a single page to the display rotation\n"
"  --page-secs N             seconds per page (0 = manual button switch only)\n"
"  --poll-ms N               sampling interval in ms (default 250)\n"
"  --indicator-cpu-percent N CPU indicator threshold in percent (default 5)\n"
"  --indicator-net-kbytes N  network indicator threshold in KB/s (default 10)\n"
"  --net-smoothing-secs N    network rate EMA time constant; 0 disables (default 2)\n"
"  --brightness 0..7         panel brightness (default 5)\n"
"  --net-interface NAME      interface for throughput (default wan)\n"
"  --self-test               run built-in tests and exit\n"
"  -v, --version             show version and exit\n"
"  -h, --help                show this help\n",
    out);
}

static void add_page(app_args_t *a, const char *name)
{
    if (a->num_pages >= PAGE_COUNT) return;
    page_id_t p = page_from_name(name);
    a->pages[a->num_pages++] = p;
}

static void parse_pages_str(app_args_t *a, const char *s)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    char *token = strtok(buf, ", \t");
    while (token) {
        add_page(a, token);
        token = strtok(NULL, ", \t");
    }
}

int app_parse_args(app_args_t *a, int argc, char **argv)
{
    memset(a, 0, sizeof(*a));
    a->page_secs = 0; /* default 0: manual flip only unless configured */
    a->poll_ms = 250;
    a->screen_on = true;
    a->custom_text = "ATHENA";
    a->indicator_cpu_percent = 5.0;
    a->indicator_net_kbytes = 10.0;
    a->net_smoothing_secs = 2.0;
    a->brightness = 5;
    a->net_if = "wan";
    a->num_pages = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--sim") == 0 || strcmp(arg, "--test") == 0) {
            a->sim = true;
        } else if (strcmp(arg, "--no-clear") == 0) {
            a->no_clear = true;
        } else if (strcmp(arg, "--self-test") == 0) {
            a->self_test = true;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("athena-led-c %s\n", ATHENA_LED_VERSION);
            return 1;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 1;
        } else if (strcmp(arg, "--pages") == 0 && i + 1 < argc) {
            parse_pages_str(a, argv[++i]);
        } else if (strcmp(arg, "--page") == 0 && i + 1 < argc) {
            add_page(a, argv[++i]);
        } else if (strcmp(arg, "--page-secs") == 0 && i + 1 < argc) {
            a->page_secs = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(arg, "--poll-ms") == 0 && i + 1 < argc) {
            a->poll_ms = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(arg, "--indicator-cpu-percent") == 0 && i + 1 < argc) {
            a->indicator_cpu_percent = strtod(argv[++i], NULL);
        } else if (strcmp(arg, "--indicator-net-kbytes") == 0 && i + 1 < argc) {
            a->indicator_net_kbytes = strtod(argv[++i], NULL);
        } else if (strcmp(arg, "--net-smoothing-secs") == 0 && i + 1 < argc) {
            a->net_smoothing_secs = strtod(argv[++i], NULL);
        } else if (strcmp(arg, "--screen-on") == 0 && i + 1 < argc) {
            a->screen_on = (atoi(argv[++i]) != 0);
        } else if (strcmp(arg, "--brightness") == 0 && i + 1 < argc) {
            a->brightness = (uint8_t)(strtoul(argv[++i], NULL, 10) & 0x07);
        } else if (strcmp(arg, "--custom-text") == 0 && i + 1 < argc) {
            a->custom_text = argv[++i];
        } else if (strcmp(arg, "--net-interface") == 0 && i + 1 < argc) {
            a->net_if = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            usage(stderr);
            return -1;
        }
    }

    if (a->num_pages == 0) {
        a->pages[0] = PAGE_TIME;
        a->num_pages = 1;
    }
    if (a->poll_ms == 0) a->poll_ms = 250;
    if (a->indicator_cpu_percent < 0.0) a->indicator_cpu_percent = 0.0;
    if (a->indicator_net_kbytes < 0.0) a->indicator_net_kbytes = 0.0;
    if (a->net_smoothing_secs < 0.0) a->net_smoothing_secs = 0.0;
    return 0;
}

int main(int argc, char **argv)
{
    app_args_t args;
    int rc = app_parse_args(&args, argc, argv);
    if (rc != 0)
        return rc < 0 ? 2 : 0;

    pages_set_custom_text(args.custom_text);

    if (args.self_test)
        return app_self_test();

    return app_run(&args);
}
