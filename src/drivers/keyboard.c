/* ZenithOS v6.0 - PS/2 keyboard driver (scancode set 1, E0 prefix aware). */
#include "keyboard.h"
#include "hal/x86/port.h"
#include "config.h"

static char ring[KBD_BUF_SIZE];
static volatile unsigned rpos = 0, wpos = 0;
static uint32_t held = 0;   /* latched "currently down" bits  */
static uint32_t edge = 0;   /* latched "pressed since clear"  */
static int shift = 0;
static int e0 = 0;

static void enq(char c) {
    unsigned next = (wpos + 1) & (KBD_BUF_SIZE - 1);
    if (next == rpos) return;   /* full: drop */
    ring[wpos] = c;
    wpos = next;
}

static char kbd_char(uint8_t sc) {
    switch (sc) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x10: return shift ? 'Q' : 'q';
        case 0x11: return shift ? 'W' : 'w';
        case 0x12: return shift ? 'E' : 'e';
        case 0x13: return shift ? 'R' : 'r';
        case 0x14: return shift ? 'T' : 't';
        case 0x15: return shift ? 'Y' : 'y';
        case 0x16: return shift ? 'U' : 'u';
        case 0x17: return shift ? 'I' : 'i';
        case 0x18: return shift ? 'O' : 'o';
        case 0x19: return shift ? 'P' : 'p';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x1E: return shift ? 'A' : 'a';
        case 0x1F: return shift ? 'S' : 's';
        case 0x20: return shift ? 'D' : 'd';
        case 0x21: return shift ? 'F' : 'f';
        case 0x22: return shift ? 'G' : 'g';
        case 0x23: return shift ? 'H' : 'h';
        case 0x24: return shift ? 'J' : 'j';
        case 0x25: return shift ? 'K' : 'k';
        case 0x26: return shift ? 'L' : 'l';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x2C: return shift ? 'Z' : 'z';
        case 0x2D: return shift ? 'X' : 'x';
        case 0x2E: return shift ? 'C' : 'c';
        case 0x2F: return shift ? 'V' : 'v';
        case 0x30: return shift ? 'B' : 'b';
        case 0x31: return shift ? 'N' : 'n';
        case 0x32: return shift ? 'M' : 'm';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        case 0x0E: return '\b';
        case 0x0F: return '\t';
        default:   return 0;
    }
}

static int kbd_bit(uint8_t sc) {
    switch (sc) {
        case 0x10: return KEY_Q;
        case 0x11: return KEY_W;
        case 0x12: return KEY_E;
        case 0x13: return KEY_R;
        case 0x1E: return KEY_A;
        case 0x1F: return KEY_S;
        case 0x20: return KEY_D;
        case 0x39: return KEY_SPACE;
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x01: return KEY_ESC;
        case 0x1C: return KEY_ENTER;
        case 0x0F: return KEY_TAB;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        default:   return -1;
    }
}

void keyboard_init(void) {
    uint32_t t = 100000;
    while ((inb(0x64) & 0x02) && t--) ;  /* wait input buffer empty */
    outb(0x64, 0xAE);                    /* enable first PS/2 port */
}

void keyboard_irq(void) {
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) { e0 = 1; return; }

    if (sc & 0x80) {                     /* break */
        uint8_t make = sc & 0x7F;
        if (e0) {
            if (make == 0x48) held &= ~(1u << KEY_UP);
            else if (make == 0x50) held &= ~(1u << KEY_DOWN);
            else if (make == 0x4B) held &= ~(1u << KEY_LEFT);
            else if (make == 0x4D) held &= ~(1u << KEY_RIGHT);
        } else {
            if (make == 0x2A || make == 0x36) shift = 0;
            else {
                int b = kbd_bit(make);
                if (b >= 0) held &= ~(1u << b);
            }
        }
        e0 = 0;
        return;
    }

    if (e0) {                            /* E0-prefixed make */
        e0 = 0;
        if (sc == 0x48) { held |= (1u << KEY_UP);    edge |= (1u << KEY_UP); return; }
        if (sc == 0x50) { held |= (1u << KEY_DOWN);  edge |= (1u << KEY_DOWN); return; }
        if (sc == 0x4B) { held |= (1u << KEY_LEFT);  edge |= (1u << KEY_LEFT); return; }
        if (sc == 0x4D) { held |= (1u << KEY_RIGHT); edge |= (1u << KEY_RIGHT); return; }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }

    {
        int b = kbd_bit(sc);
        char c = kbd_char(sc);
        if (b >= 0) { held |= (1u << b); edge |= (1u << b); }
        if (c) enq(c);
    }
}

int keyboard_has_char(void) { return rpos != wpos; }

char keyboard_get_char(void) {
    char c;
    while (rpos == wpos) ;
    c = ring[rpos];
    rpos = (rpos + 1) & (KBD_BUF_SIZE - 1);
    return c;
}

uint32_t keyboard_key_down(uint32_t bit) { return (held >> bit) & 1u; }

int keyboard_pressed_clear(uint32_t bit) {
    uint32_t mask = 1u << bit;
    if (edge & mask) { edge &= ~mask; return 1; }
    return 0;
}

void keyboard_clear_all(void) { edge = 0; held = 0; rpos = wpos = 0; }
