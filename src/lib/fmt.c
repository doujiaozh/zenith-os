/* ZenithOS v6.0 - kprintf: serial + optional GUI sink. */
#include "fmt.h"
#include <stdint.h>
#include <stdarg.h>
#include "drivers/serial.h"

static char_sink_t sink;

void fmt_set_sink(char_sink_t s) { sink = s; }

static void emit(char c) {
    serial_putc(c);
    if (sink) sink(c);
}

static void emit_str(const char *s) {
    while (*s) emit(*s++);
}

static void emit_uint(uint32_t v, int radix, int upper, int minw) {
    char buf[16];
    int i = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        buf[i++] = dig[v % (uint32_t)radix];
        v /= (uint32_t)radix;
    } while (v);
    while (i < minw) buf[i++] = '0';
    while (i > 0) emit(buf[--i]);
}

static void emit_int(int v, int minw) {
    if (v < 0) { emit('-'); emit_uint((uint32_t)(-v), 10, 0, minw); }
    else emit_uint((uint32_t)v, 10, 0, minw);
}

static void kvprintf(const char *fmt, va_list ap) {
    int width;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit(*fmt); continue; }
        fmt++;
        width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
            case '%':  emit('%'); break;
            case 'c':  emit((char)va_arg(ap, int)); break;
            case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; emit_str(s); break; }
            case 'd':
            case 'i':  emit_int(va_arg(ap, int), width); break;
            case 'u':  emit_uint(va_arg(ap, unsigned), 10, 0, width); break;
            case 'x':  emit_uint(va_arg(ap, unsigned), 16, 0, width); break;
            case 'X':  emit_uint(va_arg(ap, unsigned), 16, 1, width); break;
            case 'p':  emit_str("0x"); emit_uint(va_arg(ap, unsigned), 16, 0, 8); break;
            default:   emit('%'); emit(*fmt); break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

void klog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    emit('\n');
}
