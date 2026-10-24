; =============================================================================
;  ZenithOS v6.0 - kernel/startup.asm
;  First 32-bit code in the flat kernel image (_start must reside at offset 0
;  of .text, which the build places first). Receives BootInfo* in EBX.
; =============================================================================
[BITS 32]
section .text

global _start
extern kernel_main
extern __bss_start
extern __bss_end

_start:
    cli
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x0009F000
    cld

    ; Zero the BSS [__bss_start, __bss_end). Linker-provided bounds.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    shr ecx, 2
    rep stosd

    ; Call kernel_main(BootInfo*) - EBX holds the BootInfo pointer from boot.
    push ebx
    call kernel_main
    add esp, 4

.halt:
    cli
    hlt
    jmp .halt
