/* ZenithOS v6.0 - VGA mode 13h graphics driver (double-buffered). */
#include "vga.h"
#include "ui/font.h"
#include "hal/x86/port.h"
#include "lib/string.h"
#include "config.h"
#include "zenith.h"

static uint8_t *const framebuf = (uint8_t *)(uint32_t)VGA_FB;
static uint8_t *const backbuf  = (uint8_t *)(uint32_t)VGA_BACKBUF;

/* 16-color DAC palette (6-bit RGB). */
static const uint8_t pal[16][3] = {
    {  0,  0,  0 },  /* COL_BLACK    */
    { 10, 12, 20 },  /* COL_DARK     */
    {  3,  6, 14 },  /* COL_BG       */
    { 14, 18, 28 },  /* COL_PANEL    */
    { 63, 63, 63 },  /* COL_WHITE    */
    { 34, 38, 46 },  /* COL_GRAY     */
    {  0, 56, 63 },  /* COL_CYAN     */
    { 58,  0, 58 },  /* COL_MAGENTA  */
    { 63, 48,  0 },  /* COL_AMBER    */
    {  0, 52, 24 },  /* COL_GREEN    */
    { 60,  0,  8 },  /* COL_RED      */
    {  0, 32, 63 },  /* COL_BLUE     */
    { 34,  0, 58 },  /* COL_VIOLET   */
    {  0, 34, 36 },  /* COL_TEAL     */
    { 63, 26,  0 },  /* COL_ORANGE   */
    { 24, 26, 32 },  /* COL_DIMGRAY  */
};

void vga_init(void) {
    int i;
    for (i = 0; i < 16; i++) {
        outb(VGA_DAC_INDEX, (uint8_t)i);
        outb(VGA_DAC_DATA, pal[i][0]);
        outb(VGA_DAC_DATA, pal[i][1]);
        outb(VGA_DAC_DATA, pal[i][2]);
    }
    vga_clear(COL_BG);
    vga_swap();
}

void vga_clear(uint8_t color) {
    kmemset(backbuf, color, VGA_WIDTH * VGA_HEIGHT);
}

/* Scroll the back buffer up by `lines` pixels; the freed space at the
 * bottom is filled with `color`. Used by the console to keep a growing
 * shell log on screen. */
void vga_scroll(int lines, uint8_t color) {
    int row;
    if (lines <= 0) return;
    if (lines >= VGA_HEIGHT) { vga_clear(color); return; }
    kmemcpy(backbuf, backbuf + VGA_PITCH * (uint32_t)lines,
            VGA_PITCH * (uint32_t)(VGA_HEIGHT - lines));
    for (row = VGA_HEIGHT - lines; row < VGA_HEIGHT; row++)
        vga_hline(0, VGA_WIDTH - 1, row, color);
}

void vga_set_pixel(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    backbuf[y * VGA_PITCH + x] = color;
}

void vga_fill_rect(int x, int y, int w, int h, uint8_t color) {
    int yy;
    for (yy = y; yy < y + h; yy++) vga_hline(x, x + w - 1, yy, color);
}

void vga_vline(int x, int y0, int y1, uint8_t color) {
    int t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    for (; y0 <= y1; y0++) vga_set_pixel(x, y0, color);
}

void vga_hline(int x0, int x1, int y, uint8_t color) {
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    for (; x0 <= x1; x0++) vga_set_pixel(x0, y, color);
}

/* Bresenham line. */
void vga_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, e2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = dx - dy;
    for (;;) {
        vga_set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void vga_rect(int x, int y, int w, int h, uint8_t color) {
    vga_hline(x, x + w - 1, y, color);
    vga_hline(x, x + w - 1, y + h - 1, color);
    vga_vline(x, y, y + h - 1, color);
    vga_vline(x + w - 1, y, y + h - 1, color);
}

void vga_putchar(int x, int y, char c, uint8_t fg, uint8_t bg, int scale) {
    const uint8_t *g = font_glyph(c);
    int gx, gy;
    if (scale < 1) scale = 1;
    for (gy = 0; gy < FONT_HEIGHT; gy++) {
        for (gx = 0; gx < FONT_WIDTH; gx++) {
            uint8_t color = (g[gx] & (1 << gy)) ? fg : bg;
            int px = x + gx * scale, py = y + gy * scale;
            int sy, sx;
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++)
                    vga_set_pixel(px + sx, py + sy, color);
        }
    }
}

void vga_puts(int x, int y, const char *s, uint8_t fg, uint8_t bg, int scale) {
    int cx = x;
    if (scale < 1) scale = 1;
    while (*s) {
        if (*s == '\n') { cx = x; y += (FONT_HEIGHT + 1) * scale; s++; continue; }
        vga_putchar(cx, y, *s, fg, bg, scale);
        cx += (FONT_WIDTH + 1) * scale;
        s++;
    }
}

void vga_swap(void) {
    kmemcpy(framebuf, backbuf, VGA_WIDTH * VGA_HEIGHT);
}
