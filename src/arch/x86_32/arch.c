/* ZenithOS v6.0 - x86_32 architecture bring-up entry. */
#include "arch/arch.h"
#include "arch/x86_32/gdt.h"
#include "arch/x86_32/idt.h"
#include "arch/x86_32/pic.h"
#include "arch/x86_32/arch.h"
#include "hal/x86/cpu.h"

void arch_early_init(void) {
    gdt_install();
    pic_remap(X86_IRQ_BASE, X86_IRQ_BASE + 8);
    pic_unmask(0);   /* PIT  */
    pic_unmask(1);   /* kbd  */
    idt_install();
}

void arch_halt(void) {
    for (;;) cpu_hlt();
}
