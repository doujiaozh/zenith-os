/* ZenithOS v6.0 - panic: report to serial + VGA, then halt. */
#include "panic.h"
#include "fmt.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "hal/x86/cpu.h"

static const char *const exc_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "BOUND Range", "Invalid Opcode", "Device Not Available", "Double Fault",
    "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
    "Stack Fault", "General Protection", "Page Fault", "Reserved",
    "x87 FPU", "Alignment Check", "Machine Check", "SIMD FP",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved",
};

void panic(const char *msg) {
    cpu_cli();
    serial_puts("\r\n\r\n*** KERNEL PANIC ***\r\n");
    if (msg) serial_puts(msg);
    serial_puts("\r\nSystem halted.\r\n");
    vga_clear(COL_RED);
    vga_puts(16, 30, "KERNEL PANIC", COL_WHITE, COL_RED, 2);
    if (msg) vga_puts(16, 52, msg, COL_WHITE, COL_RED, 1);
    vga_puts(16, 66, "System halted.", COL_WHITE, COL_RED, 1);
    vga_swap();
    for (;;) cpu_hlt();
}

void panic_exception(uint8_t n, uint32_t err, uint32_t eip,
                     uint32_t eax, uint32_t ebx) {
    const char *name = n < 32 ? exc_names[n] : "Unknown";
    serial_puts("\r\nEXCEPTION: ");
    serial_puts(name);
    kprintf("\r\n  int=%u err=0x%x eip=0x%x eax=0x%x ebx=0x%x\r\n",
            n, err, eip, eax, ebx);
    vga_clear(COL_RED);
    vga_puts(16, 30, name, COL_WHITE, COL_RED, 2);
    {
        char line[64];
        line[0] = 0;
        /* reuse kprintf semantics for the screen via a small manual write */
    }
    vga_puts(16, 50, "Exception (see serial log)", COL_WHITE, COL_RED, 1);
    vga_swap();
    for (;;) cpu_hlt();
}
