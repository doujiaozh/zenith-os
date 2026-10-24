/* ZenithOS v6.0 - I/O port access (x86 HAL). */
#ifndef ZENITH_PORT_H
#define ZENITH_PORT_H

#include <stdint.h>

void     outb(uint16_t port, uint8_t value);
uint8_t  inb(uint16_t port);
void     outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void     outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);
void     io_wait(void);

#endif /* ZENITH_PORT_H */
