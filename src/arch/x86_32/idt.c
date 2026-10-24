/* ZenithOS v6.0 - IDT: install gates for vectors 0..47. */
#include "arch/x86_32/idt.h"
#include "hal/x86/cpu.h"
#include "lib/string.h"

typedef struct {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

/* 48 stub entry points generated in idt.asm */
extern uint32_t isr_table[48];

static idt_entry_t idt[256];
static idt_ptr_t idtp;

static void idt_set_gate(uint16_t n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].base_lo = base & 0xFFFF;
    idt[n].sel     = sel;
    idt[n].always0 = 0;
    idt[n].flags   = flags;
    idt[n].base_hi = (base >> 16) & 0xFFFF;
}

void idt_install(void) {
    int i;
    kmemset(idt, 0, sizeof(idt));
    for (i = 0; i < 48; i++)
        idt_set_gate((uint16_t)i, isr_table[i], 0x08, 0x8E);
    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)&idt[0];
    cpu_lidt(&idtp);
}
