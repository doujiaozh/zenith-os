/* ZenithOS v6.0 - interrupt dispatch (exceptions -> panic, IRQs -> drivers). */
#ifndef ZENITH_ISR_H
#define ZENITH_ISR_H

#include <stdint.h>

typedef struct regs {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed)) regs_t;

void isr_handler(regs_t *r);

/* Install a driver IRQ handler (0..15). The handler runs with interrupts
 * off, must ack its device, and must not block. NULL is never accepted.
 * Returns 0 on success, -1 for an out-of-range IRQ. */
int irq_install_handler(uint8_t irq, void (*fn)(void));

#endif /* ZENITH_ISR_H */
