#include "sim.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static bool g_clear = true;
static struct termios g_old_termios;
static bool g_termios_saved = false;

void sim_set_clear(bool enable)
{
    g_clear = enable;
}

void sim_print_frame(const uint8_t cols[27], uint8_t status)
{
    if (g_clear)
        fputs("\033[H\033[2J", stdout);

    fputs("┌", stdout);
    for (int x = 0; x < 27; x++) fputs("─", stdout);
    fputs("┐\n", stdout);

    for (int y = 0; y < 5; y++) {
        fputs("│", stdout);
        for (int x = 0; x < 27; x++) {
            bool on = (cols[x] & (1u << y)) != 0;
            fputs(on ? "■" : "·", stdout);
        }
        fputs("│\n", stdout);
    }

    fputs("└", stdout);
    for (int x = 0; x < 27; x++) fputs("─", stdout);
    fputs("┘\n", stdout);

    /* Four discrete indicator LEDs below the matrix. */
    fputs("LED ", stdout);
    fputs((status & 0x01) ? "🕐" : "·", stdout);
    fputs((status & 0x02) ? "🏅" : "·", stdout);
    fputs((status & 0x04) ? "▲" : "·", stdout);
    fputs((status & 0x08) ? "▼" : "·", stdout);
    fputs("\n[q] quit  [n/p] page  [space] screen on/off\n", stdout);
    fflush(stdout);
}

void sim_input_init(void)
{
    if (tcgetattr(STDIN_FILENO, &g_old_termios) == 0) {
        struct termios t = g_old_termios;
        t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
            g_termios_saved = true;
    }
}

void sim_input_restore(void)
{
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_old_termios);
        g_termios_saved = false;
    }
}

char sim_input_read(void)
{
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return n == 1 ? c : 0;
}
