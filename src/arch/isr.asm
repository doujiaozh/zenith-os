; =============================================================================
;  ZenithOS - arch/isr.asm
;  Interrupt stubs, the common trap frame, and the IRQ dispatcher.
;
;  Every vector gets a uniform frame:
;
;      [vector][error][eip][cs][eflags]        (hardware frame + our pushes)
;      then PUSHA, then DS/ES/FS/GS
;
;  Exceptions 0..31 route to exception_panic (lib/panic.asm) with the vector,
;  error code and faulting EIP. IRQ stubs (32..47) route through the handler
;  table; the dispatcher sends the EOI, so drivers never touch the PIC
;  directly. Unused IDT gates point at isr_unused, which simply returns.
;
;  isr_stub_table is consumed by arch/idt.asm to build the IDT in a loop, which
;  keeps the vector list single-sourced.
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
extern pit_irq_handler
extern kbd_irq_handler
extern exception_panic
extern pic_send_eoi
%endif

; -----------------------------------------------------------------------------
; Exception stubs.  Vectors 8, 10..14, 17, 21, 29, 30 push an error code in
; hardware; all others get a synthetic zero.
; -----------------------------------------------------------------------------
%macro ISR_NOERR 1
isr_%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_%1:
    push dword %1
    jmp isr_common
%endmacro

%assign i 0
%rep 32
%if i = 8 || i = 10 || i = 11 || i = 12 || i = 13 || i = 14 || i = 17 || i = 21 || i = 29 || i = 30
    ISR_ERR i
%else
    ISR_NOERR i
%endif
%assign i i + 1
%endrep

; -----------------------------------------------------------------------------
; Hardware IRQ stubs: vectors 32..47
; -----------------------------------------------------------------------------
%macro IRQ_STUB 1
irq_%1:
    push dword 0
    push dword 32 + %1
    jmp isr_common
%endmacro

%assign i 0
%rep 16
    IRQ_STUB i
%assign i i + 1
%endrep

; Stray software interrupt gate: return quietly.
isr_unused:
    push dword 0
    push dword 0xFFFF
    jmp isr_common

; -----------------------------------------------------------------------------
; Common trap entry
; -----------------------------------------------------------------------------
isr_common:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld

    mov eax, [esp + 48]             ; vector
    cmp eax, 32
    jb .exception
    cmp eax, 48
    jb .irq
    jmp .return                     ; unused vector

.exception:
    mov edx, [esp + 52]             ; error code
    mov ecx, [esp + 56]             ; faulting EIP
    call exception_panic            ; never returns
    jmp .return

.irq:
    call irq_dispatch

.return:
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                      ; drop vector + error code
    iret

; -----------------------------------------------------------------------------
; irq_dispatch: EAX = vector 32..47
; Calls the registered handler, then acknowledges the PIC.
; -----------------------------------------------------------------------------
irq_dispatch:
    sub eax, 32
    cmp eax, 16
    jae .done
    mov edi, eax                    ; preserve the IRQ line across the handler
    mov esi, [irq_handler_table + eax * 4]
    call esi
    mov eax, edi
    call pic_send_eoi
.done:
    ret

; Default handler for masked/unimplemented IRQ lines.
irq_default_handler:
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
irq_handler_table:
    dd pit_irq_handler              ; IRQ0  - 8254 timer
    dd kbd_irq_handler              ; IRQ1  - PS/2 keyboard
    dd irq_default_handler          ; IRQ2  - cascade
    dd irq_default_handler          ; IRQ3  - COM2
    dd irq_default_handler          ; IRQ4  - COM1
    dd irq_default_handler          ; IRQ5  - LPT2
    dd irq_default_handler          ; IRQ6  - floppy
    dd irq_default_handler          ; IRQ7  - LPT1 / spurious
    dd irq_default_handler          ; IRQ8  - RTC
    dd irq_default_handler          ; IRQ9
    dd irq_default_handler          ; IRQ10
    dd irq_default_handler          ; IRQ11
    dd irq_default_handler          ; IRQ12 - PS/2 mouse
    dd irq_default_handler          ; IRQ13 - FPU
    dd irq_default_handler          ; IRQ14 - primary ATA
    dd irq_default_handler          ; IRQ15 - secondary ATA

align 4
isr_stub_table:
%assign i 0
%rep 32
    dd isr_%+i
%assign i i + 1
%endrep
%assign i 0
%rep 16
    dd irq_%+i
%assign i i + 1
%endrep
