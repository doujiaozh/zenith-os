/* ZenithOS v6.0 - 8259A PIC remap + EOI (x86_32). */
#include "arch/x86_32/pic.h"
#include "hal/x86/port.h"

void pic_remap(uint8_t off1, uint8_t off2) {
    uint8_t m1 = inb(PIC1_DATA), m2 = inb(PIC2_DATA);
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, off1); io_wait();
    outb(PIC2_DATA, off2); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();   /* slave at IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();   /* 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port = PIC1_DATA;
    if (irq >= 8) { irq -= 8; port = PIC2_DATA; }
    outb(port, inb(port) | (uint8_t)(1 << irq));
}

void pic_unmask(uint8_t irq) {
    uint16_t port = PIC1_DATA;
    if (irq >= 8) { irq -= 8; port = PIC2_DATA; }
    outb(port, inb(port) & (uint8_t)~(1 << irq));
}

void pic_disable(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
