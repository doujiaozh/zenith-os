; =============================================================================
;  ZenithOS - arch/idt.asm
;  256-entry IDT: CPU exceptions 0..31 and remapped IRQs 32..47 come from the
;  single-sourced isr_stub_table; every other gate points at isr_unused.
;  All gates are 32-bit ring-0 interrupt gates (DPL 0, type 0xE, present).
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
extern isr_stub_table
extern isr_unused
%endif

IDT_ENTRIES equ 256
IDT_ATTR    equ 0x8E00             ; present, DPL0, 32-bit interrupt gate

; -----------------------------------------------------------------------------
; idt_init: build and load the IDT.  Interrupts must remain disabled.
; -----------------------------------------------------------------------------
idt_init:
    pushad

    ; Fill all 256 gates with the harmless stub.
    mov edi, idt_table
    mov ecx, IDT_ENTRIES
    mov edx, isr_unused
.fill:
    mov eax, edx
    call idt_write_gate
    add edi, 8
    loop .fill

    ; Override the 32 exception + 16 IRQ vectors from the stub table.
    mov esi, isr_stub_table
    mov edi, idt_table
    mov ecx, 48
.vectors:
    mov eax, [esi]
    call idt_write_gate
    add esi, 4
    add edi, 8
    loop .vectors

    lidt [idt_descriptor]
    popad
    ret

; -----------------------------------------------------------------------------
; idt_write_gate: EAX = handler offset, EDI = gate pointer
; -----------------------------------------------------------------------------
idt_write_gate:
    mov [edi + 0], ax               ; offset 15..0
    mov word [edi + 2], 0x08        ; kernel code selector
    mov word [edi + 4], IDT_ATTR    ; type + attributes
    shr eax, 16
    mov [edi + 6], ax               ; offset 31..16
    ret

; -----------------------------------------------------------------------------
; idt_set_gate: EAX = vector, EBX = handler offset, ECX = gate attributes
; Used by the syscall layer to install INT 0x80 with DPL 3.
; -----------------------------------------------------------------------------
idt_set_gate:
    push edi
    push eax
    mov edi, idt_table
    shl eax, 3
    add edi, eax
    mov eax, ebx
    mov [edi + 0], ax
    mov word [edi + 2], 0x08
    mov [edi + 4], cx
    shr eax, 16
    mov [edi + 6], ax
    pop eax
    pop edi
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 8
idt_descriptor:
    dw IDT_ENTRIES * 8 - 1
    dd idt_table

align 8
idt_table:
    times IDT_ENTRIES * 8 db 0
