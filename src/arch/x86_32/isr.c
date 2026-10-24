/* ZenithOS v6.0 - interrupt dispatch. Exceptions panic; IRQs reach drivers.
 *
 * IRQ routing: drivers (e.g. the NIC) install themselves with
 * irq_install_handler(irq, fn); a registered handler wins over the built-in
 * core routing. The table lives in .bss (zeroed at boot) so uninstalled IRQs
 * fall back to pit_tick/keyboard_irq exactly as before.
 */
#include "arch/x86_32/isr.h"
#include "arch/x86_32/pic.h"
#include "arch/x86_32/arch.h"
#include "drivers/pit.h"
#include "drivers/keyboard.h"
#include "lib/panic.h"
#include <stddef.h>

static void (*irq_table[16])(void);

int irq_install_handler(uint8_t irq, void (*fn)(void)) {
    if (irq >= 16 || fn == NULL) return -1;
    irq_table[irq] = fn;
    return 0;
}

void isr_handler(regs_t *r) {
    uint32_t n = r->int_no;
    if (n < 32) {
        panic_exception((uint8_t)n, r->err_code, r->eip, r->eax, r->ebx);
        return;
    }
    if (n >= X86_IRQ_BASE && n < X86_IRQ_BASE + 16) {
        uint8_t irq = (uint8_t)(n - X86_IRQ_BASE);
        if (irq_table[irq])              /* driver handler wins */
            irq_table[irq]();
        else if (irq == 0) pit_tick();
        else if (irq == 1) keyboard_irq();
        pic_send_eoi(irq);
    }
}
