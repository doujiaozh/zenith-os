/* ZenithOS v6.0 - VGA mode 13h graphics driver with a 5x7 text renderer. */
#ifndef ZENITH_VGA_H
#define ZENITH_VGA_H

#include <stdint.h>

/* stable palette slots (assigned in vga_init) */
#define COL_BLACK    0
#define COL_DARK     1
#define COL_BG       2
#define COL_PANEL    3
#define COL_WHITE    4
#define COL_GRAY     5
#define COL_CYAN     6
#define COL_MAGENTA  7
#define COL_AMBER    8
#define COL_GREEN    9
#define COL_RED      10
#define COL_BLUE     11
#define COL_VIOLET   12
#define COL_TEAL     13
#define COL_ORANGE   14
#define COL_DIMGRAY  15

void vga_init(void);
void vga_clear(uint8_t color);
void vga_set_pixel(int x, int y, uint8_t color);
void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
void vga_vline(int x, int y0, int y1, uint8_t color);
void vga_hline(int x0, int x1, int y, uint8_t color);
void vga_line(int x0, int y0, int x1, int y1, uint8_t color);
void vga_rect(int x, int y, int w, int h, uint8_t color);
void vga_putchar(int x, int y, char c, uint8_t fg, uint8_t bg, int scale);
void vga_puts(int x, int y, const char *s, uint8_t fg, uint8_t bg, int scale);
void vga_swap(void);

#endif /* ZENITH_VGA_H */
