/* ZenithOS v6.0 - GDT (flat 0..4 GiB code/data, kernel + user). */
#include "arch/x86_32/gdt.h"
#include "arch/x86_32/arch.h"
#include "hal/x86/cpu.h"

typedef struct {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt[5];
static gdt_ptr_t gp;

/* declared in gdt_asm.asm */
extern void asm_reload_segments(void);

static void gdt_set(int n, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[n].limit_lo = limit & 0xFFFF;
    gdt[n].base_lo  = base & 0xFFFF;
    gdt[n].base_mid = (base >> 16) & 0xFF;
    gdt[n].access   = access;
    gdt[n].gran     = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[n].base_hi  = (base >> 24) & 0xFF;
}

void gdt_install(void) {
    gdt_set(0, 0, 0x00000000, 0x00, 0x00);          /* null                */
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);          /* kernel code (ring0) */
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);          /* kernel data (ring0) */
    gdt_set(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);          /* user code   (ring3) */
    gdt_set(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);          /* user data   (ring3) */
    gp.limit = (uint16_t)(sizeof(gdt) - 1);
    gp.base  = (uint32_t)&gdt[0];
    cpu_lgdt(&gp);
    asm_reload_segments();
}
