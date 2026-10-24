/*
 * ============================================================================
 *  ZenithOS v6.0 - kernel/kernel_main.c
 *  C entry point reached from startup.asm (BootInfo* in EBX).
 *
 *  Boot order: serial/VGA console -> memory bring-up (E820 -> PMM bitmap ->
 *  identity paging -> kernel heap, done with interrupts off) -> interrupt
 *  system (IDT + PIC + PIT) -> the interactive shell: a "ZenithOS> " prompt
 *  with character echo, Backspace editing, and the commands
 *  help / clear / reboot / 3d.
 *
 *  Output goes to the VGA text layer on screen and is mirrored to COM1 so
 *  every shell session also exists as a plain serial log.
 * ============================================================================
 */
#include "zenith.h"
#include "hal/x86/port.h"
#include "hal/x86/cpu.h"
#include "arch/x86_32/arch.h"
#include "arch/x86_32/idt.h"
#include "arch/x86_32/pic.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "mm/mm.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/fmt.h"
#include "ui/font.h"
#include "ui/render3d.h"

#define PROMPT   "ZenithOS> "
#define LINE_MAX 64

/* --- text layer on the mode 13h screen (5x7 font, 1 px spacing) ----------- */
#define TERM_MARGIN 4
#define TERM_COLS   (VGA_WIDTH / (FONT_WIDTH + 1))      /* 52 */
#define TERM_ROWS   (VGA_HEIGHT / (FONT_HEIGHT + 1))    /* 24 */
#define CELL_W      (FONT_WIDTH + 1)
#define CELL_H      (FONT_HEIGHT + 1)

static int cur_x = 0, cur_y = 0;
static uint8_t term_fg = COL_WHITE, term_bg = COL_BG;

/* --- terminal primitives -------------------------------------------------- */
static void term_newline(void) {
    cur_x = 0;
    cur_y++;
    if (cur_y >= TERM_ROWS) {
        vga_scroll(CELL_H, term_bg);   /* scroll up one text row */
        cur_y = TERM_ROWS - 1;
    }
}

/* one printable character at the cursor; advances (and wraps) the cursor */
static void term_char(char c) {
    vga_putchar(TERM_MARGIN + cur_x * CELL_W, TERM_MARGIN + cur_y * CELL_H,
                c, term_fg, term_bg, 1);
    cur_x++;
    if (cur_x >= TERM_COLS) term_newline();
}

/* echo a typed keystroke: screen + COM1, so serial logs hold the whole
 * interactive session (input and output), not just program output */
static void term_echo(char c) {
    term_char(c);
    serial_putc(c);
    vga_swap();                /* live echo */
}

/* a whole string; '\n' moves to the next row; mirrored to COM1 */
static void term_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            term_newline();
            serial_puts("\r\n");
        } else {
            term_char(*s);
            serial_putc(*s);
        }
        s++;
    }
    vga_swap();
}

static void term_clear(void) {
    vga_clear(term_bg);
    vga_swap();
    cur_x = 0;
    cur_y = 0;
}

/* erase the character left of the cursor (screen + serial echo) */
static void term_backspace(void) {
    if (cur_x <= 0) return;
    cur_x--;
    vga_putchar(TERM_MARGIN + cur_x * CELL_W, TERM_MARGIN + cur_y * CELL_H,
                ' ', term_fg, term_bg, 1);
    vga_swap();
    serial_puts("\b \b");
}

/* --- line editor ---------------------------------------------------------- */
/* Read one line into buf (echoing as we go), honouring Backspace and Enter.
 * Returns the length; buf is NUL-terminated. */
static int read_line(char *buf, int cap) {
    int len = 0;
    for (;;) {
        char c;
        while (!keyboard_has_char())
            cpu_hlt();                 /* sleep until any IRQ (keyboard/PIT) */
        c = keyboard_get_char();

        if (c == '\n') break;          /* Enter: end of line */
        if (c == '\b') {               /* Backspace: rub out one character */
            if (len > 0) { len--; term_backspace(); }
            continue;
        }
        if (c >= 32 && c < 127 && len < cap - 1) {
            buf[len++] = c;
            term_echo(c);
        }
    }
    term_newline();
    serial_puts("\r\n");
    buf[len] = 0;
    return len;
}

/* --- commands ------------------------------------------------------------- */
static void cmd_help(void) {
    term_puts("commands:\n");
    term_puts("  help    show this list\n");
    term_puts("  clear   clear the screen\n");
    term_puts("  3d      rotating 3D neural grid (ESC returns here)\n");
    term_puts("  reboot  restart the machine\n");
}

static void cmd_reboot(void) {
    term_puts("rebooting...\n");
    cpu_cli();
    while (inb(0x64) & 0x02) ;  /* wait for the PS/2 input buffer to drain */
    outb(0x64, 0xFE);           /* pulse the reset line through the KBC */
    for (;;) cpu_hlt();         /* if that fails, halt forever */
}

/* --- shell ---------------------------------------------------------------- */
static void shell(void) {
    static char line[LINE_MAX];

    for (;;) {
        term_puts(PROMPT);
        if (read_line(line, LINE_MAX) == 0) continue;

        if (!kstrcmp(line, "help"))          cmd_help();
        else if (!kstrcmp(line, "clear"))    term_clear();
        else if (!kstrcmp(line, "reboot"))   cmd_reboot();
        else if (!kstrcmp(line, "3d")) {
            render3d_run();       /* returns after ESC; screen is cleared */
            term_clear();         /* fresh shell surface */
        } else {
            term_puts("no such command: ");
            term_puts(line);
            term_puts("\n");
        }
    }
}

/* --- entry ---------------------------------------------------------------- */
void kernel_main(BootInfo *info) {
    serial_init();
    serial_puts("\r\n");
    serial_puts("========================================\r\n");
    serial_puts(" ZenithOS v6.0  [x86_32]  booting...\r\n");
    serial_puts("========================================\r\n");

    /* The E820 map PMM walks comes from the loader; refuse to build memory
     * state on a hand-off we cannot trust. */
    if (info->magic != BOOTINFO_MAGIC || info->version < BOOTINFO_VERSION) {
        serial_puts("boot: fatal: BootInfo magic/version mismatch\r\n");
        for (;;) cpu_hlt();
    }

    vga_init();
    term_clear();

    /* memory bring-up: E820 -> 4 KiB PMM bitmap -> identity paging (16 MiB,
     * covers kernel/heap/bitmaps/VGA/stack) -> kernel heap.  The MMU flip
     * must happen with interrupts off; the shell wakes them up later. */
    cpu_cli();
    mm_init(info);
    klog("mm: identity paging on, %u MiB mapped, %u free / %u used 4 KiB pages\r\n",
         (unsigned)(IDENT_MAP_BYTES >> 20),
         (unsigned)pmm_free_pages(), (unsigned)pmm_used_pages());

    /* device bring-up: keyboard, 100 Hz system tick, then the interrupt
     * system (IDT installed, PIC remapped to vectors 0x20..0x2F). */
    keyboard_init();
    pit_init(PIT_HZ);
    idt_install();
    pic_remap(X86_IRQ_BASE, X86_IRQ_BASE + 8);
    pic_unmask(1);                  /* IRQ 1: keyboard */
    pic_unmask(0);                  /* IRQ 0: system tick for animations */
    cpu_sti();

    term_puts("ZenithOS v6.0 - interactive shell\n");
    term_puts("type 'help' for commands.\n");
    term_newline();

    shell();

    /* unreachable: the shell only leaves via reboot */
    for (;;) cpu_hlt();
}
