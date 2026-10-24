/*
 * ============================================================================
 *  ZenithOS v6.0 - kernel/kernel_main.c
 *  The C entry point reached from startup.asm. BootInfo* is the only argument.
 * ============================================================================
 */
#include "zenith.h"
#include "hal/x86/port.h"
#include "hal/x86/cpu.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "lib/string.h"

void kernel_main(BootInfo *info) {
    (void)info;

    serial_init();
    serial_puts("\r\n");
    serial_puts("========================================\r\n");
    serial_puts(" ZenithOS v6.0  [x86_32]  booting...\r\n");
    serial_puts("========================================\r\n");

    vga_init();
    vga_clear(COL_BG);
    vga_puts(4, 4, "ZENITHOS v6.0", COL_CYAN, COL_BG, 2);
    vga_puts(4, 22, "kernel: C + ASM (HAL)", COL_WHITE, COL_BG, 1);
    vga_puts(4, 32, "C kernel_main is alive.", COL_GREEN, COL_BG, 1);
    vga_swap();

    serial_puts("VGA framebuffer ready.\r\n");
    serial_puts("Entering idle loop.\r\n");

    for (;;) cpu_hlt();
}
