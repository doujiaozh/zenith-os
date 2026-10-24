/* ZenithOS v6.0 - x86 (IA-32) architecture definitions. */
#ifndef ZENITH_ARCH_X86_32_H
#define ZENITH_ARCH_X86_32_H

/* flat segment selectors (shared with boot and kernel GDT) */
#define X86_SEL_CODE0 0x08
#define X86_SEL_DATA0 0x10
#define X86_SEL_CODE3 0x18
#define X86_SEL_DATA3 0x20

/* IDT gate flags */
#define X86_IDT_PRESENT 0x80
#define X86_IDT_INT_GATE 0x0E
#define X86_IDT_TRAP_GATE 0x0F
#define X86_IDT_RING0 0x00
#define X86_IDT_RING3 0x60

/* interrupt vector base after PIC remap (IRQ0 -> 0x20) */
#define X86_IRQ_BASE 0x20
#define X86_IRQ_PIT  0x20
#define X86_IRQ_KBD  0x21

#endif /* ZENITH_ARCH_X86_32_H */
