/* ZenithOS v6.0 - 16550 UART, polled. */
#ifndef ZENITH_SERIAL_H
#define ZENITH_SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
int  serial_rx_ready(void);
char serial_getc(void);

#endif /* ZENITH_SERIAL_H */
