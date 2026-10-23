; =============================================================================
;  ZenithOS - drivers/keyboard.asm
;  PS/2 keyboard on IRQ1 (scancode set 1).  The interrupt handler keeps a
;  32-bit "held" mask for continuous controls (the WASD camera) and a 64-entry
;  ring of make events for discrete actions.
;
;  API
;    kbd_init
;    kbd_poll        -> EAX = 0 when empty, else (0x100 | logical key)
;    kbd_held_mask   -> dword, bit N set while logical key N is held
;    kbd_scan_count  -> dword, number of scancodes received (diagnostics)
;
;  Logical keys live in src/include/config.inc (KEY_W .. KEY_C).
; =============================================================================

[bits 32]

%include "include/config.inc"
%include "include/macros.inc"

%ifdef ZENITH_OBJ_CHECK
extern pic_unmask
%endif

; -----------------------------------------------------------------------------
; kbd_init: reset state and unmask IRQ1
; -----------------------------------------------------------------------------
kbd_init:
    mov dword [kbd_held_mask], 0
    mov dword [kbd_head], 0
    mov dword [kbd_tail], 0
    mov dword [kbd_scan_count], 0
    mov byte [kbd_e0], 0
    mov eax, 1
    call pic_unmask
    ret

; -----------------------------------------------------------------------------
; kbd_irq_handler: IRQ1. Read the controller output buffer and translate.
; -----------------------------------------------------------------------------
kbd_irq_handler:
    pushad
    in al, 0x60
    inc dword [kbd_scan_count]
    mov bl, al

    cmp byte [kbd_e0], 0
    je .normal
    mov byte [kbd_e0], 0
    mov al, bl
    and al, 0x7F
    call kbd_map_extended
    jmp .mapped

.normal:
    mov al, bl
    cmp al, 0xE0
    jne .plain
    mov byte [kbd_e0], 1
    jmp .done
.plain:
    and al, 0x7F
    call kbd_map_normal

.mapped:
    cmp al, 0xFF
    je .done
    movzx eax, al
    test bl, 0x80
    jz .make
    btr dword [kbd_held_mask], eax
    jmp .done
.make:
    bts dword [kbd_held_mask], eax
    call kbd_push_event
.done:
    popad
    ret

; -----------------------------------------------------------------------------
; kbd_push_event: EAX = logical key
; -----------------------------------------------------------------------------
kbd_push_event:
    push eax
    push ecx
    push edx
    mov ecx, [kbd_head]
    mov edx, ecx
    and edx, KBD_BUF_SIZE - 1
    mov [kbd_ring + edx], al
    inc ecx
    mov [kbd_head], ecx
    mov edx, ecx
    sub edx, [kbd_tail]
    cmp edx, KBD_BUF_SIZE
    jbe .done
    inc dword [kbd_tail]
.done:
    pop edx
    pop ecx
    pop eax
    ret

; -----------------------------------------------------------------------------
; kbd_poll: EAX = 0x100 | key, or 0 when the queue is empty
; -----------------------------------------------------------------------------
kbd_poll:
    mov eax, [kbd_tail]
    cmp eax, [kbd_head]
    je .empty
    mov ecx, eax
    and ecx, KBD_BUF_SIZE - 1
    movzx eax, byte [kbd_ring + ecx]
    inc dword [kbd_tail]
    or eax, 0x100
    ret
.empty:
    xor eax, eax
    ret

; -----------------------------------------------------------------------------
; kbd_map_normal: AL = scancode (break bit cleared) -> AL = logical key / 0xFF
; -----------------------------------------------------------------------------
kbd_map_normal:
    cmp al, 0x11
    je .w
    cmp al, 0x1E
    je .a
    cmp al, 0x1F
    je .s
    cmp al, 0x20
    je .d
    cmp al, 0x10
    je .q
    cmp al, 0x12
    je .e
    cmp al, 0x13
    je .r
    cmp al, 0x39
    je .space
    cmp al, 0x01
    je .esc
    cmp al, 0x1C
    je .enter
    cmp al, 0x2E
    je .c
    mov al, 0xFF
    ret
.w:
    mov al, KEY_W
    ret
.a:
    mov al, KEY_A
    ret
.s:
    mov al, KEY_S
    ret
.d:
    mov al, KEY_D
    ret
.q:
    mov al, KEY_Q
    ret
.e:
    mov al, KEY_E
    ret
.r:
    mov al, KEY_R
    ret
.space:
    mov al, KEY_SPACE
    ret
.esc:
    mov al, KEY_ESC
    ret
.enter:
    mov al, KEY_ENTER
    ret
.c:
    mov al, KEY_C
    ret

; -----------------------------------------------------------------------------
; kbd_map_extended: AL = extended scancode (break bit cleared)
; -----------------------------------------------------------------------------
kbd_map_extended:
    cmp al, 0x48
    je .up
    cmp al, 0x50
    je .down
    cmp al, 0x4B
    je .left
    cmp al, 0x4D
    je .right
    mov al, 0xFF
    ret
.up:
    mov al, KEY_UP
    ret
.down:
    mov al, KEY_DOWN
    ret
.left:
    mov al, KEY_LEFT
    ret
.right:
    mov al, KEY_RIGHT
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
kbd_held_mask dd 0
kbd_head      dd 0
kbd_tail      dd 0
kbd_scan_count dd 0
kbd_e0        db 0
kbd_ring      times KBD_BUF_SIZE db 0
