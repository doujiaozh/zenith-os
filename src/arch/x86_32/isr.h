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

#endif /* ZENITH_ISR_H */
