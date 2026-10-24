/* ZenithOS v6.0 - 5x7 kernel font (public domain bitmap data). */
#ifndef ZENITH_FONT_H
#define ZENITH_FONT_H

#include <stdint.h>

#define FONT_WIDTH  5
#define FONT_HEIGHT 7
#define FONT_FIRST  32
#define FONT_LAST   126

/* 5 column bytes for the glyph (bit 0 = top). */ 
const uint8_t *font_glyph(char c);

#endif /* ZENITH_FONT_H */
