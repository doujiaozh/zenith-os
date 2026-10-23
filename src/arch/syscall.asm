; =============================================================================
;  ZenithOS - arch/syscall.asm
;  A minimal kernel-call ABI on INT 0x80 (native, NOT POSIX yet).  The gate is
;  installed with DPL 3 so a future ring-3 userspace can use the same vector
;  unchanged; today the self-test calls it from ring 0 to prove the ABI.
;
;    EAX = 0  SYS_TICKS       -> EAX = system ticks
;    EAX = 1  SYS_WRITE       -> EBX = string, ECX = length; EAX = bytes
;    EAX = 2  SYS_SNN_ENERGY  -> EAX = current SNN activation energy
;    EAX = 3  SYS_HEAP_USED   -> EAX = kernel heap bytes in use
;    EAX = 4  SYS_PMM_FREE_KB -> EAX = free physical KiB
;    otherwise                -> EAX = -1
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
extern idt_set_gate
extern pit_ticks
extern serial_putc
extern snn_energy
extern heap_used_bytes
extern pmm_free_kb
%endif

SYS_TICKS       equ 0
SYS_WRITE       equ 1
SYS_SNN_ENERGY  equ 2
SYS_HEAP_USED   equ 3
SYS_PMM_FREE_KB equ 4

; -----------------------------------------------------------------------------
; syscall_init: install the INT 0x80 gate (DPL 3)
; -----------------------------------------------------------------------------
syscall_init:
    mov eax, 0x80
    mov ebx, syscall_entry
    mov ecx, 0xEE00                  ; present, DPL3, 32-bit interrupt gate
    call idt_set_gate
    ret

; -----------------------------------------------------------------------------
; syscall_entry: hardware frame is EIP/CS/EFLAGS only
; -----------------------------------------------------------------------------
syscall_entry:
    pusha
    call syscall_dispatch
    mov [syscall_result], eax
    popa
    mov eax, [syscall_result]
    iret

; -----------------------------------------------------------------------------
; syscall_dispatch: EAX = call number, EBX/ECX = arguments
; -----------------------------------------------------------------------------
syscall_dispatch:
    cmp eax, SYS_WRITE
    je .write
    cmp eax, SYS_SNN_ENERGY
    je .energy
    cmp eax, SYS_HEAP_USED
    je .heap
    cmp eax, SYS_PMM_FREE_KB
    je .pmm
    cmp eax, SYS_TICKS
    je .ticks
    mov eax, 0xFFFFFFFF
    ret
.ticks:
    mov eax, [pit_ticks]
    ret
.energy:
    mov eax, [snn_energy]
    ret
.heap:
    call heap_used_bytes
    ret
.pmm:
    call pmm_free_kb
    ret
.write:
    push ebx
    push ecx
    mov edx, ecx
    mov esi, ebx
.next:
    test edx, edx
    jz .done
    lodsb
    call serial_putc
    dec edx
    jmp .next
.done:
    mov eax, ecx
    pop ecx
    pop ebx
    ret

align 4
syscall_result dd 0
