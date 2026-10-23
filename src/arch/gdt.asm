; =============================================================================
;  ZenithOS - arch/gdt.asm
;  The kernel's own GDT: null, flat 32-bit ring-0 code (0x08), flat 32-bit
;  ring-0 data (0x10).  The boot sector installs an equivalent table so it can
;  enter protected mode; the kernel reloads this one as the first arch
;  subsystem, which keeps the bootloader and the kernel independently valid.
;
;  A TSS is deliberately absent in v1.0 (no ring 3 yet); adding one is part of
;  the v2.0 microkernel milestone (see ROADMAP.md).
; =============================================================================

[bits 32]

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

GDT_CODE_SEL equ 0x08
GDT_DATA_SEL equ 0x10

; -----------------------------------------------------------------------------
; gdt_init: load the kernel GDT and reload every segment selector.
; -----------------------------------------------------------------------------
gdt_init:
    lgdt [gdt_descriptor]
    mov ax, GDT_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp GDT_CODE_SEL:.flush_cs
.flush_cs:
    ret

; -----------------------------------------------------------------------------
; GDT image
; -----------------------------------------------------------------------------
align 8
gdt_start:
    dq 0x0000000000000000           ; 0x00 null descriptor
gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A                        ; present, ring 0, code, readable
    db 0xCF                        ; 4 KiB granularity, 32-bit, limit 0xFFFFF
    db 0x00
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                        ; present, ring 0, data, writable
    db 0xCF
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
