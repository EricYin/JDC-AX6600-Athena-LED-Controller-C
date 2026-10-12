#ifndef ATHENA_FONT_H
#define ATHENA_FONT_H

#include <stddef.h>
#include <stdint.h>

/* Maximum columns a rendered line may occupy (scrolling handled by caller). */
#define FONT_MAX_COLS 512

/*
 * Render ASCII text into 27x5 matrix columns.
 * Each column is 5 vertical bits (bit 0 = top row, bit 4 = bottom row).
 * Unknown characters are skipped. A one-column gap is inserted between glyphs.
 * Returns the number of columns written (0 for empty text).
 */
size_t font_render(const char *text, uint8_t *cols, size_t max_cols);

/* Exposed for unit tests / simulator reuse. */
const uint8_t *font_glyph(char c, size_t *width);

#endif /* ATHENA_FONT_H */
