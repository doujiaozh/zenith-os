; =============================================================================
;  ZenithOS - arch/pic.asm
;  8259A programmable interrupt controller.  The BIOS leaves IRQ0..7 at
;  vectors 0x08..0x0F, where they collide with CPU exceptions, so the first
;  thing the kernel does is remap the pair to 0x20..0x2F and mask everything.
;  Drivers unmask only the lines they actually own.
;
;  API
;    pic_init              remap to 0x20/0x28, mask all
;    pic_unmask : EAX=irq  clear the mask bit (handles the cascade line)
;    pic_mask   : EAX=irq  set the mask bit
;    pic_send_eoi : EAX=irq
;    pic_mask_all
; =============================================================================

[bits 32]

%include "include/macros.inc"

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

PIC1_CMD  equ 0x20
PIC1_DATA equ 0x21
PIC2_CMD  equ 0xA0
PIC2_DATA equ 0xA1

; -----------------------------------------------------------------------------
; pic_init: standard ICW1..ICW4 remap sequence
; -----------------------------------------------------------------------------
pic_init:
    OUTB PIC1_CMD, 0x11             ; ICW1: edge triggered, cascade, ICW4 needed
    IO_DELAY
    OUTB PIC2_CMD, 0x11
    IO_DELAY
    OUTB PIC1_DATA, 0x20            ; ICW2: master vector base 0x20
    IO_DELAY
    OUTB PIC2_DATA, 0x28            ; ICW2: slave vector base 0x28
    IO_DELAY
    OUTB PIC1_DATA, 0x04            ; ICW3: slave on IRQ2
    IO_DELAY
    OUTB PIC2_DATA, 0x02            ; ICW3: cascade identity
    IO_DELAY
    OUTB PIC1_DATA, 0x01            ; ICW4: 8086 mode
    IO_DELAY
    OUTB PIC2_DATA, 0x01
    IO_DELAY
    OUTB PIC1_DATA, 0xFF            ; mask everything until drivers are ready
    OUTB PIC2_DATA, 0xFF
    ret

; -----------------------------------------------------------------------------
; pic_unmask: EAX = IRQ line 0..15
; -----------------------------------------------------------------------------
pic_unmask:
    push ecx
    push edx
    cmp eax, 8
    jb .master
    sub eax, 8
    mov ecx, eax
    mov dl, 1
    shl dl, cl
    not dl
    in al, PIC2_DATA
    and al, dl
    out PIC2_DATA, al
    ; Unmask the cascade (IRQ2) on the master whenever a slave line is used.
    in al, PIC1_DATA
    and al, 0xFB
    out PIC1_DATA, al
    jmp .done
.master:
    mov ecx, eax
    mov dl, 1
    shl dl, cl
    not dl
    in al, PIC1_DATA
    and al, dl
    out PIC1_DATA, al
.done:
    pop edx
    pop ecx
    ret

; -----------------------------------------------------------------------------
; pic_mask: EAX = IRQ line 0..15
; -----------------------------------------------------------------------------
pic_mask:
    push ecx
    push edx
    cmp eax, 8
    jb .master
    sub eax, 8
    mov ecx, eax
    mov dl, 1
    shl dl, cl
    in al, PIC2_DATA
    or al, dl
    out PIC2_DATA, al
    jmp .done
.master:
    mov ecx, eax
    mov dl, 1
    shl dl, cl
    in al, PIC1_DATA
    or al, dl
    out PIC1_DATA, al
.done:
    pop edx
    pop ecx
    ret

; -----------------------------------------------------------------------------
; pic_send_eoi: EAX = IRQ line
; -----------------------------------------------------------------------------
pic_send_eoi:
    cmp eax, 8
    jb .master
    OUTB PIC2_CMD, 0x20
.master:
    OUTB PIC1_CMD, 0x20
    ret

; -----------------------------------------------------------------------------
; pic_mask_all
; -----------------------------------------------------------------------------
pic_mask_all:
    OUTB PIC1_DATA, 0xFF
    OUTB PIC2_DATA, 0xFF
    ret
