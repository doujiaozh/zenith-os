; GDT segment reload (x86_32). Reloads CS via a far jump, then the data
; selectors. Assumes the caller runs in the flat 0x08 code segment.
[BITS 32]
section .text

; both spellings exported (TCC/Linux GCC bare, mingw GCC underscored)
global asm_reload_segments
global _asm_reload_segments
asm_reload_segments:
_asm_reload_segments:
    jmp 0x08:.reload
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
