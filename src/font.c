#include "font.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    char ch;
    uint8_t cols[5];
    uint8_t width;
} glyph_t;

/* 5-row dot font, one byte per column, bit0=top .. bit4=bottom. */
static const glyph_t GLYPHS[] = {
    {'0', {0x1f, 0x11, 0x1f}, 3},
    {'1', {0x11, 0x1f, 0x10}, 3},
    {'2', {0x19, 0x15, 0x13}, 3},
    {'3', {0x11, 0x15, 0x1f}, 3},
    {'4', {0x07, 0x04, 0x1f}, 3},
    {'5', {0x17, 0x15, 0x1d}, 3},
    {'6', {0x1f, 0x15, 0x1d}, 3},
    {'7', {0x01, 0x01, 0x1f}, 3},
    {'8', {0x1f, 0x15, 0x1f}, 3},
    {'9', {0x17, 0x15, 0x1f}, 3},
    {'A', {0x1e, 0x05, 0x05, 0x1e}, 4},
    {'B', {0x1f, 0x15, 0x0a}, 3},
    {'C', {0x0e, 0x11, 0x11}, 3},
    {'D', {0x1f, 0x11, 0x0e}, 3},
    {'E', {0x1f, 0x15, 0x15}, 3},
    {'F', {0x1f, 0x05, 0x05}, 3},
    {'G', {0x0e, 0x11, 0x1d}, 3},
    {'H', {0x1f, 0x04, 0x1f}, 3},
    {'I', {0x11, 0x1f, 0x11}, 3},
    {'J', {0x18, 0x10, 0x0f}, 3},
    {'K', {0x1f, 0x04, 0x1b}, 3},
    {'L', {0x1f, 0x10, 0x10}, 3},
    {'M', {0x1f, 0x02, 0x1f}, 3},
    {'N', {0x1f, 0x02, 0x04, 0x1f}, 4},
    {'O', {0x0e, 0x11, 0x0e}, 3},
    {'P', {0x1f, 0x05, 0x02}, 3},
    {'Q', {0x0e, 0x11, 0x1e}, 3},
    {'R', {0x1f, 0x05, 0x1a}, 3},
    {'S', {0x12, 0x15, 0x09}, 3},
    {'T', {0x01, 0x1f, 0x01}, 3},
    {'U', {0x1f, 0x10, 0x1f}, 3},
    {'V', {0x0f, 0x10, 0x0f}, 3},
    {'W', {0x1f, 0x08, 0x1f}, 3},
    {'X', {0x1b, 0x04, 0x1b}, 3},
    {'Y', {0x03, 0x1c, 0x03}, 3},
    {'Z', {0x19, 0x15, 0x13}, 3},
    {' ', {0x00}, 1},
    {'-', {0x04, 0x04, 0x04}, 3},
    {'_', {0x10, 0x10, 0x10}, 3},
    {'=', {0x0a, 0x0a, 0x0a}, 3},
    {'+', {0x04, 0x0e, 0x04}, 3},
    {'*', {0x0a, 0x04, 0x0a}, 3},
    {'/', {0x18, 0x06, 0x01}, 3},
    {'\\', {0x01, 0x06, 0x18}, 3},
    {'.', {0x10}, 1},
    {',', {0x10, 0x08}, 2},
    {':', {0x00, 0x0a, 0x00}, 3},
    {';', {0x00, 0x00, 0x00}, 3}, /* blank digit slot */
    {'^', {0x0a}, 1}, /* compact colon for time-with-seconds */
    {'~', {0x08, 0x10, 0x08}, 3},
    {'%', {0x19, 0x08, 0x13}, 3},
    {'?', {0x01, 0x15, 0x03}, 3},
    {'(', {0x0e, 0x11}, 2},
    {')', {0x11, 0x0e}, 2},
    {'C', {0x0e, 0x11, 0x11}, 3}, /* placeholder, overwritten below */
};

const uint8_t *font_glyph(char c, size_t *width)
{
    static const uint8_t degree_c[] = {0x03, 0x00, 0x0e, 0x11, 0x11};

    if (c == '\xC') { /* 'C degree' pseudo glyph used by temperature pages */
        *width = 5;
        return degree_c;
    }

    char up = (char)toupper((unsigned char)c);
    size_t n = sizeof(GLYPHS) / sizeof(GLYPHS[0]);
    /* Linear scan is tiny (< 60 entries) and avoids any lookup table RAM. */
    for (size_t i = 0; i < n; i++) {
        if (GLYPHS[i].ch == up) {
            *width = GLYPHS[i].width;
            return GLYPHS[i].cols;
        }
    }
    *width = 0;
    return NULL;
}

size_t font_render(const char *text, uint8_t *cols, size_t max_cols)
{
    size_t out = 0;

    if (!text || !cols || max_cols == 0)
        return 0;

    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        size_t w = 0;
        const uint8_t *g = font_glyph((char)*p, &w);
        if (!g)
            continue;

        for (size_t j = 0; j < w; j++) {
            if (out >= max_cols)
                return out;
            cols[out++] = g[j];
        }
        if (out < max_cols && *(p + 1) != '\0')
            cols[out++] = 0x00; /* inter-glyph gap */
    }
    return out;
}
