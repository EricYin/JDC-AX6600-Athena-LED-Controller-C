/*
 * TM1628A hardware driver for the JDCloud AX6600 (RE-CS-02) front panel.
 *
 * The panel has two TM1628A controllers (left 14 columns + right 13 columns)
 * sharing CLK/DIO; STB selects the target chip:
 *   STB_LEFT  = TLMM 69, STB_RIGHT = TLMM 70, CLK = TLMM 73, DIO = TLMM 74
 *
 * Backends:
 *   cdev  - /dev/gpiochipN ioctl (modern kernels, preferred)
 *   sysfs - /sys/class/gpio (legacy firmware fallback)
 */
#define _GNU_SOURCE
#include "screen.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PIN_STB_LEFT  69u
#define PIN_STB_RIGHT 70u
#define PIN_CLK       73u
#define PIN_DIO       74u

#define CMD_DISPLAY_MODE 0x03
#define CMD_DATA_MODE    0x40
#define CMD_ADDR         0xC0

#include <linux/gpio.h>

typedef struct {
    int fd;                 /* cdev line-handle fd, -1 when sysfs */
    int fd_stb_l, fd_stb_r, fd_clk, fd_dio; /* sysfs value fds */
    uint8_t cdev_values[4]; /* cached cdev line values */
} bus_t;

static bus_t bus = { .fd = -1, .fd_stb_l = -1, .fd_stb_r = -1,
                     .fd_clk = -1, .fd_dio = -1 };

/* slot index for the four requested lines */
enum { LINE_STB_L = 0, LINE_STB_R = 1, LINE_CLK = 2, LINE_DIO = 3 };

long screen_detect_gpio_base(void)
{
    long base = 512;
    DIR *d = opendir("/sys/class/gpio");
    if (!d) return base;

    struct dirent *de;
    long best_base = -1, best_ngpio = -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "gpiochip", 8) != 0) continue;
        char path[320], label[64] = "";
        long b = -1, n = -1;

        snprintf(path, sizeof(path), "/sys/class/gpio/%s/base", de->d_name);
        FILE *f = fopen(path, "r");
        if (f) { if (fscanf(f, "%ld", &b) != 1) b = -1; fclose(f); }

        snprintf(path, sizeof(path), "/sys/class/gpio/%s/ngpio", de->d_name);
        f = fopen(path, "r");
        if (f) { if (fscanf(f, "%ld", &n) != 1) n = -1; fclose(f); }

        snprintf(path, sizeof(path), "/sys/class/gpio/%s/label", de->d_name);
        f = fopen(path, "r");
        if (f) { if (fgets(label, sizeof(label), f)) {} fclose(f); }

        bool is_main = strstr(label, "pinctrl") != NULL || strstr(label, "tlmm") != NULL;
        if (is_main || (best_base < 0 && n > best_ngpio)) {
            best_base = b; best_ngpio = n;
        }
    }
    closedir(d);
    return best_base >= 0 ? best_base : base;
}

static bool cdev_find_main_chip(char *path, size_t pathlen)
{
    DIR *d = opendir("/dev");
    if (!d) return false;

    char best[320] = "";
    uint32_t best_lines = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "gpiochip", 8) != 0) continue;
        char cur[320];
        snprintf(cur, sizeof(cur), "/dev/%s", de->d_name);
        int fd = open(cur, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;

        struct gpiochip_info info;
        memset(&info, 0, sizeof(info));
        bool ok = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0;
        close(fd);
        if (!ok) continue;

        bool is_main = strstr(info.label, "pinctrl") != NULL ||
                       strstr(info.label, "tlmm") != NULL;
        if (best[0] == '\0' || (is_main && info.lines > best_lines)) {
            snprintf(best, sizeof(best), "%s", cur);
            best_lines = info.lines;
        }
    }
    closedir(d);

    if (best[0] == '\0') return false;
    snprintf(path, pathlen, "%s", best);
    return true;
}

static bool cdev_request_outputs(void)
{
    char chip[64];
    if (!cdev_find_main_chip(chip, sizeof(chip))) return false;

    int fd = open(chip, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[LINE_STB_L] = PIN_STB_LEFT;
    req.lineoffsets[LINE_STB_R] = PIN_STB_RIGHT;
    req.lineoffsets[LINE_CLK]   = PIN_CLK;
    req.lineoffsets[LINE_DIO]   = PIN_DIO;
    req.default_values[LINE_STB_L] = 1; /* STB idle high */
    req.default_values[LINE_STB_R] = 1;
    req.default_values[LINE_CLK]   = 0;
    req.default_values[LINE_DIO]   = 0;
    req.lines = 4;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    snprintf(req.consumer_label, sizeof(req.consumer_label), "athena-led-c");

    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        close(fd);
        return false;
    }
    close(fd);
    bus.fd = req.fd;
    bus.cdev_values[LINE_STB_L] = 1;
    bus.cdev_values[LINE_STB_R] = 1;
    bus.cdev_values[LINE_CLK] = 0;
    bus.cdev_values[LINE_DIO] = 0;
    return true;
}

static int sysfs_export_pin(long num)
{
    int fd = open("/sys/class/gpio/export", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%ld", num);
    ssize_t n = write(fd, buf, (size_t)len);
    close(fd);
    return n == len ? 0 : -1;
}

static int sysfs_open_value(long num, int flags)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%ld/value", num);
    return open(path, flags | O_CLOEXEC);
}

static int sysfs_write_str(const char *path, const char *str)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = write(fd, str, strlen(str));
    close(fd);
    return n == (ssize_t)strlen(str) ? 0 : -1;
}

static bool sysfs_init(long base)
{
    long pins[4] = { base + PIN_STB_LEFT, base + PIN_STB_RIGHT,
                     base + PIN_CLK, base + PIN_DIO };

    for (int i = 0; i < 4; i++)
        if (sysfs_export_pin(pins[i]) != 0)
            return false;

    char path[96];
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%ld/direction", pins[i]);
        if (sysfs_write_str(path, "out") != 0)
            return false;
    }
    bus.fd_stb_l = sysfs_open_value(pins[0], O_WRONLY);
    bus.fd_stb_r = sysfs_open_value(pins[1], O_WRONLY);
    bus.fd_clk   = sysfs_open_value(pins[2], O_WRONLY);
    bus.fd_dio   = sysfs_open_value(pins[3], O_WRONLY);

    if (bus.fd_stb_l < 0 || bus.fd_stb_r < 0 || bus.fd_clk < 0 || bus.fd_dio < 0)
        return false;
    if (write(bus.fd_stb_l, "1", 1) != 1 || write(bus.fd_stb_r, "1", 1) != 1)
        return false;
    return true;
}

static bool bus_set(int line, uint8_t value)
{
    if (bus.fd >= 0) {
        bus.cdev_values[line] = value ? 1 : 0;
        struct gpiohandle_data data;
        memcpy(data.values, bus.cdev_values, sizeof(bus.cdev_values));
        return ioctl(bus.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) == 0;
    }
    int fd = line == LINE_STB_L ? bus.fd_stb_l
           : line == LINE_STB_R ? bus.fd_stb_r
           : line == LINE_CLK   ? bus.fd_clk
                                : bus.fd_dio;
    char c = (char)('0' + value);
    return write(fd, &c, 1) == 1;
}

static bool write_bit(uint8_t bit)
{
    /* TM1628A latches DIO on CLK rising edge. */
    return bus_set(LINE_CLK, 0) && bus_set(LINE_DIO, bit) && bus_set(LINE_CLK, 1);
}

/*
 * TM1628A command bytes are standard 8-bit instructions: LSB first.
 * (display mode 0x03, data mode 0x40, address 0xC0, power 0x88|br)
 */
static bool write_command_byte(uint8_t cmd)
{
    for (int i = 0; i < 8; i++) {
        if (!write_bit((cmd >> i) & 1)) return false;
    }
    return true;
}

/*
 * TM1628A display data is a special 11-bit format: 5 data bits followed by
 * 6 address/pad bits (sent low). Only odd-indexed bytes carry the pad.
 */
static bool write_data_byte(uint8_t value, bool fill_six_low)
{
    for (int i = 0; i < 5; i++) {
        if (!write_bit((value >> i) & 1)) return false;
    }
    if (fill_six_low) {
        for (int i = 0; i < 6; i++) {
            if (!write_bit(0)) return false;
        }
    }
    return true;
}

static bool unit_write(int stb_line, uint8_t cmd, const uint8_t *values, size_t n)
{
    if (!bus_set(stb_line, 0)) return false;
    if (!write_command_byte(cmd)) return false;
    for (size_t i = 0; i < n; i++) {
        if (!write_data_byte(values[i], (i % 2) != 0)) return false;
    }
    return bus_set(stb_line, 1);
}

bool screen_init(screen_t *s, gpio_backend_t backend, long gpio_base)
{
    memset(s, 0, sizeof(*s));
    s->fd = -1; s->fd_stb_l = s->fd_stb_r = s->fd_clk = s->fd_dio = -1;

    if (backend == GPIO_BACKEND_CDEV || backend == GPIO_BACKEND_AUTO) {
        if (cdev_request_outputs()) {
            s->fd = bus.fd;
            goto configured;
        }
        if (backend == GPIO_BACKEND_CDEV) return false;
    }
    if (backend == GPIO_BACKEND_SYSFS || backend == GPIO_BACKEND_AUTO) {
        long base = gpio_base > 0 ? gpio_base : screen_detect_gpio_base();
        if (!sysfs_init(base)) return false;
        goto configured;
    }
    return false;

configured:
    {
        screen_t tmp = *s;
        (void)tmp;
    }
    if (!unit_write(LINE_STB_L, CMD_DISPLAY_MODE, NULL, 0)) return false;
    if (!unit_write(LINE_STB_R, CMD_DISPLAY_MODE, NULL, 0)) return false;
    if (!unit_write(LINE_STB_L, CMD_DATA_MODE, NULL, 0)) return false;
    if (!unit_write(LINE_STB_R, CMD_DATA_MODE, NULL, 0)) return false;
    return true;
}

bool screen_power(screen_t *s, bool on, uint8_t brightness)
{
    if (bus.fd < 0 && bus.fd_stb_l < 0) return false;
    uint8_t cmd = on ? (uint8_t)(0x88 | (brightness & 0x07)) : 0x80;
    if (!unit_write(LINE_STB_L, cmd, NULL, 0)) return false;
    if (!unit_write(LINE_STB_R, cmd, NULL, 0)) return false;
    s->powered = on;
    s->brightness = on ? (uint8_t)(brightness & 0x07) : 0;
    return true;
}

bool screen_write_frame(const screen_t *s, const uint8_t cols[SCREEN_COLS], uint8_t status)
{
    if (!s->powered) return false;
    uint8_t left[14];
    uint8_t right[14];
    memcpy(left, cols, sizeof(left));
    memcpy(right, cols + 14, 13);
    right[13] = status;
    if (!unit_write(LINE_STB_L, CMD_ADDR, left, sizeof(left))) return false;
    return unit_write(LINE_STB_R, CMD_ADDR, right, sizeof(right));
}

void screen_close(screen_t *s)
{
    if (bus.fd >= 0) close(bus.fd);
    if (bus.fd_stb_l >= 0) close(bus.fd_stb_l);
    if (bus.fd_stb_r >= 0) close(bus.fd_stb_r);
    if (bus.fd_clk >= 0) close(bus.fd_clk);
    if (bus.fd_dio >= 0) close(bus.fd_dio);
    bus.fd = -1;
    bus.fd_stb_l = bus.fd_stb_r = bus.fd_clk = bus.fd_dio = -1;
    s->fd = -1;
    s->fd_stb_l = s->fd_stb_r = s->fd_clk = s->fd_dio = -1;
    s->powered = false;
}
