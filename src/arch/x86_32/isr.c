/* ZenithOS v6.0 - interrupt dispatch. Exceptions panic; IRQs reach drivers. */
#include "arch/x86_32/isr.h"
#include "arch/x86_32/pic.h"
#include "arch/x86_32/arch.h"
#include "drivers/pit.h"
#include "drivers/keyboard.h"
#include "lib/panic.h"

void isr_handler(regs_t *r) {
    uint32_t n = r->int_no;
    if (n < 32) {
        panic_exception((uint8_t)n, r->err_code, r->eip, r->eax, r->ebx);
        return;
    }
    if (n >= X86_IRQ_BASE && n < X86_IRQ_BASE + 16) {
        uint8_t irq = (uint8_t)(n - X86_IRQ_BASE);
        if (irq == 0) pit_tick();
        else if (irq == 1) keyboard_irq();
        pic_send_eoi(irq);
    }
}
