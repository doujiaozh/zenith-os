; =============================================================================
;  ZenithOS - lib/panic.asm
;  Bugcheck screen.  On any CPU exception the trap frame reaches
;  exception_panic(vector, error, eip); this module writes the complete record
;  to COM1 first (so CI/serial logs keep the cause even if video is broken),
;  then paints a readable fault report into mode 13h and halts forever.
;
;  Deliberately last-resort: it only uses already-initialised VGA primitives,
;  no heap, no interrupts.
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
extern serial_puts
extern vga_clear
extern vga_fill_rect
extern vga_draw_text
extern vga_draw_text_big
extern vga_present
extern u32_to_hex
%endif

; -----------------------------------------------------------------------------
; exception_panic: EAX = vector, EDX = error code, ECX = faulting EIP
; Never returns.
; -----------------------------------------------------------------------------
exception_panic:
    cli
    mov [panic_vector], eax
    mov [panic_error], edx
    mov [panic_eip], ecx
    mov eax, cr0
    mov [panic_cr0], eax
    mov eax, cr2
    mov [panic_cr2], eax
    mov eax, cr3
    mov [panic_cr3], eax

    ; ---- serial record ------------------------------------------------------
    mov esi, panic_serial_head
    call serial_puts
    mov eax, [panic_vector]
    mov edi, panic_hex
    call u32_to_hex
    mov esi, panic_hex
    call serial_puts
    mov esi, panic_serial_err
    call serial_puts
    mov eax, [panic_error]
    mov edi, panic_hex
    call u32_to_hex
    mov esi, panic_hex
    call serial_puts
    mov esi, panic_serial_eip
    call serial_puts
    mov eax, [panic_eip]
    mov edi, panic_hex
    call u32_to_hex
    mov esi, panic_hex
    call serial_puts
    mov esi, panic_crlf
    call serial_puts

    ; ---- video report -------------------------------------------------------
    xor eax, eax
    call vga_clear
    mov eax, 0
    mov ebx, 0
    mov ecx, VGA_WIDTH
    mov edx, 4
    mov esi, COL_RED2
    call vga_fill_rect
    mov eax, 0
    mov ebx, VGA_HEIGHT - 4
    mov ecx, VGA_WIDTH
    mov edx, 4
    mov esi, COL_RED2
    call vga_fill_rect

    mov eax, 16
    mov ebx, 24
    mov esi, panic_title
    mov edx, COL_RED
    call vga_draw_text_big

    mov eax, 16
    mov ebx, 60
    mov esi, panic_subtitle
    mov edx, COL_STEEL
    call vga_draw_text

    ; VECTOR
    mov eax, 16
    mov ebx, 82
    mov esi, panic_label_vector
    mov edx, COL_STEEL
    call vga_draw_text
    mov eax, 16 + 42
    mov ebx, 82
    mov ecx, [panic_vector]
    mov edx, COL_WHITE2
    call panic_draw_hex

    ; Exception name (vectors 0..31 only).
    mov eax, 16
    mov ebx, 94
    mov esi, panic_label_name
    mov edx, COL_STEEL
    call vga_draw_text
    mov eax, [panic_vector]
    cmp eax, 32
    jae .no_name
    mov esi, [exception_names + eax * 4]
    mov eax, 16 + 42
    mov ebx, 94
    mov edx, COL_CYAN
    call vga_draw_text
.no_name:

    ; ERROR CODE
    mov eax, 16
    mov ebx, 106
    mov esi, panic_label_error
    mov edx, COL_STEEL
    call vga_draw_text
    mov eax, 16 + 42
    mov ebx, 106
    mov ecx, [panic_error]
    mov edx, COL_WHITE2
    call panic_draw_hex

    ; EIP
    mov eax, 16
    mov ebx, 118
    mov esi, panic_label_eip
    mov edx, COL_STEEL
    call vga_draw_text
    mov eax, 16 + 42
    mov ebx, 118
    mov ecx, [panic_eip]
    mov edx, COL_WHITE2
    call panic_draw_hex

    ; CR2
    mov eax, 16
    mov ebx, 130
    mov esi, panic_label_cr2
    mov edx, COL_STEEL
    call vga_draw_text
    mov eax, 16 + 42
    mov ebx, 130
    mov ecx, [panic_cr2]
    mov edx, COL_WHITE2
    call panic_draw_hex

    ; CR0
    mov eax, 16
    mov ebx, 142
    mov esi, panic_label_cr0
    mov edx, COL_STEEL
    call vga_draw_text
    mov eax, 16 + 42
    mov ebx, 142
    mov ecx, [panic_cr0]
    mov edx, COL_WHITE2
    call panic_draw_hex

    mov eax, 16
    mov ebx, 164
    mov esi, panic_hint
    mov edx, COL_RED
    call vga_draw_text
    mov eax, 16
    mov ebx, 176
    mov esi, panic_hint2
    mov edx, COL_STEEL
    call vga_draw_text
    call vga_present

.halt:
    cli
    hlt
    jmp .halt

; -----------------------------------------------------------------------------
; panic_draw_hex: EAX = x, EBX = y, ECX = value, EDX = colour
; -----------------------------------------------------------------------------
panic_draw_hex:
    push eax
    push ebx
    push edx
    mov eax, ecx
    mov edi, panic_hex
    call u32_to_hex
    pop edx
    pop ebx
    pop eax
    mov esi, panic_hex
    call vga_draw_text
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
panic_vector dd 0
panic_error  dd 0
panic_eip    dd 0
panic_cr0    dd 0
panic_cr2    dd 0
panic_cr3    dd 0
panic_hex    times 12 db 0

panic_title         db "KERNEL PANIC", 0
panic_subtitle      db "ZenithOS v1.0 // unrecoverable CPU exception", 0
panic_label_vector  db "VECTOR", 0
panic_label_name    db "NAME", 0
panic_label_error   db "ERROR", 0
panic_label_eip     db "EIP", 0
panic_label_cr2     db "CR2", 0
panic_label_cr0     db "CR0", 0
panic_hint          db "SYSTEM HALTED - RESET REQUIRED", 0
panic_hint2         db "full record was written to COM1", 0

panic_serial_head   db 13, 10, "PANIC vector=0x", 0
panic_serial_err    db " error=0x", 0
panic_serial_eip    db " eip=0x", 0
panic_crlf          db 13, 10, 0

; Names for vectors 0..31 (the table is only indexed below 32).
exception_names:
    dd exc_0,  exc_1,  exc_2,  exc_3,  exc_4,  exc_5,  exc_6,  exc_7
    dd exc_8,  exc_9,  exc_10, exc_11, exc_12, exc_13, exc_14, exc_15
    dd exc_16, exc_17, exc_18, exc_19, exc_20, exc_21, exc_22, exc_23
    dd exc_24, exc_25, exc_26, exc_27, exc_28, exc_29, exc_30, exc_31

exc_0  db "DIVIDE ERROR", 0
exc_1  db "DEBUG", 0
exc_2  db "NMI", 0
exc_3  db "BREAKPOINT", 0
exc_4  db "OVERFLOW", 0
exc_5  db "BOUND RANGE", 0
exc_6  db "INVALID OPCODE", 0
exc_7  db "DEVICE NOT AVAILABLE", 0
exc_8  db "DOUBLE FAULT", 0
exc_9  db "COPROCESSOR OVERRUN", 0
exc_10 db "INVALID TSS", 0
exc_11 db "SEGMENT ABSENT", 0
exc_12 db "STACK FAULT", 0
exc_13 db "GENERAL PROTECTION", 0
exc_14 db "PAGE FAULT", 0
exc_15 db "RESERVED", 0
exc_16 db "X87 FLOAT", 0
exc_17 db "ALIGNMENT CHECK", 0
exc_18 db "MACHINE CHECK", 0
exc_19 db "SIMD FLOAT", 0
exc_20 db "VIRT EXCEPTION", 0
exc_21 db "CONTROL PROTECTION", 0
exc_22 db "RESERVED", 0
exc_23 db "RESERVED", 0
exc_24 db "RESERVED", 0
exc_25 db "RESERVED", 0
exc_26 db "RESERVED", 0
exc_27 db "RESERVED", 0
exc_28 db "HYPERVISOR INJECT", 0
exc_29 db "VMM COMMUNICATION", 0
exc_30 db "SECURITY", 0
exc_31 db "RESERVED", 0
