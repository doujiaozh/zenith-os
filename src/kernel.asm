; =============================================================================
;  ZenithOS v1.0 - src/kernel.asm
;  The flat 32-bit kernel entry point and module composition root.
;
;  Every subsystem lives in its own file and is assembled individually by
;  build.js with -DZENITH_OBJ_CHECK (which forces cross-module dependencies to
;  be declared explicitly).  This file is the single translation unit that
;  %includes them in link order and produces build/kernel.bin.
;
;  Boot contract (from boot.asm):
;    - protected mode, flat segments, interrupts disabled
;    - ESP = KERNEL_STACK_TOP, EBX = BOOTINFO_ADDR
;    - VGA mode 13h already selected by the BIOS
; =============================================================================

[bits 32]

%include "include/config.inc"
%include "include/macros.inc"

[org KERNEL_ORG]

; The boot sector jumps to KERNEL_ORG, i.e. byte 0 of this flat image.  Keep a
; short branch here as the image's entry vector; the entry routine itself lives
; after the modules so the file stays organised like a link map.
    jmp kernel_entry

; ---- lib --------------------------------------------------------------------
%include "lib/string.asm"
%include "lib/fixed.asm"

; ---- drivers / arch ---------------------------------------------------------
%include "drivers/serial.asm"
%include "arch/gdt.asm"
%include "arch/pic.asm"
%include "arch/idt.asm"
%include "arch/isr.asm"
%include "arch/syscall.asm"
%include "lib/panic.asm"
%include "drivers/pit.asm"
%include "drivers/rtc.asm"
%include "drivers/keyboard.asm"
%include "drivers/vga.asm"
%include "ui/font.asm"
%include "ui/theme.asm"

; ---- memory / AI ------------------------------------------------------------
%include "mm/pmm.asm"
%include "mm/heap.asm"
%include "ai/snn.asm"
%include "lib/log.asm"
%include "lib/selftest.asm"

; ---- desktop ----------------------------------------------------------------
%include "ui/neural.asm"

; =============================================================================
;  Entry point
; =============================================================================
kernel_entry:
    cli
    cld
    mov esp, KERNEL_STACK_TOP
    xor ebp, ebp

    call serial_init
    call log_init
    mov esi, msg_entry
    call log_info

    call gdt_init
    mov esi, msg_gdt
    call log_info
    call pmm_init
    mov esi, msg_pmm
    call log_info
    call heap_init
    mov esi, msg_heap
    call log_info
    call pic_init
    call idt_init
    mov esi, msg_idt
    call log_info
    call syscall_init
    call pit_init
    xor eax, eax
    call pic_unmask                 ; IRQ0 = PIT heartbeat
    call rtc_init
    call kbd_init
    mov esi, msg_irq
    call log_info
    call snn_init
    mov esi, msg_snn
    call log_info
    call theme_init
    call vga_init
    mov esi, msg_vga
    call log_info
    call ui_init
    mov esi, msg_ui
    call log_info

    ; Only now may hardware interrupts be delivered.
    sti
    mov esi, msg_sti
    call log_info

    call selftest_run
    mov esi, msg_ready
    call log_info

; -----------------------------------------------------------------------------
;  Cooperative main loop: ui_frame blocks on the next 100 Hz heartbeat, does
;  input + SNN inference + rendering, and copies the back buffer to the VGA
;  aperture.  Interrupts (timer, keyboard) preempt this loop.
; -----------------------------------------------------------------------------
.main_loop:
    call ui_frame
    jmp .main_loop

; =============================================================================
;  Boot log strings
; =============================================================================
msg_entry db "ZenithOS v1.0 kernel entered (protected mode, flat segments)", 0
msg_gdt   db "arch: GDT reloaded, flat 4 GiB ring-0 segments", 0
msg_pmm   db "mm: physical page allocator armed from E820", 0
msg_heap  db "mm: kernel heap free-list online", 0
msg_idt   db "arch: IDT + PIC remapped to 0x20/0x28, exceptions gated", 0
msg_irq   db "drivers: PIT 100 Hz, IRQ1 keyboard, CMOS RTC", 0
msg_snn   db "ai: 24-neuron spiking network online", 0
msg_vga   db "video: VGA mode 13h back-buffered 320x200x256", 0
msg_ui    db "ui: neon neural core renderer initialised", 0
msg_sti   db "kernel: interrupts enabled, perception loop running", 0
msg_ready db "ZenithOS v1.0 boot complete", 0
