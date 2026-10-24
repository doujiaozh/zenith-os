/* ZenithOS v6.0 - 16550 UART driver (COM1), polled TX/RX. */
#include "serial.h"
#include "hal/x86/port.h"
#include "config.h"
#include "zenith.h"

#define COM1 SERIAL_PORT

static int tx_ready(void) { return inb(COM1 + 5) & 0x20; }

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable interrupts     */
    outb(COM1 + 3, 0x80);   /* enable DLAB            */
    outb(COM1 + 0, SERIAL_BAUD_DIV & 0xff);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1                    */
    outb(COM1 + 2, 0xC7);   /* enable/clear FIFO      */
    outb(COM1 + 4, 0x0B);   /* IRQs enabled, RTS/DSR  */
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while (!tx_ready()) ;
    outb(COM1, (unsigned char)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

int serial_rx_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

char serial_getc(void) {
    while (!serial_rx_ready()) ;
    return (char)inb(COM1);
}
