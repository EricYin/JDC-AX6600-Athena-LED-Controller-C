#ifndef ATHENA_SIM_H
#define ATHENA_SIM_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Render a 27x5 frame to stdout using '■' and '·'. */
void sim_print_frame(const uint8_t cols[27], uint8_t status);

/* Enable/disable ANSI screen clearing. */
void sim_set_clear(bool enable);

/* Prepare terminal for non-blocking keyboard input. */
void sim_input_init(void);
void sim_input_restore(void);
/* Returns: 'q' quit, 'n' next page, 'p' previous page, ' ' toggle power, 0 none. */
char sim_input_read(void);

#endif
