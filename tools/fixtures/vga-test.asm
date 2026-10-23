; ---------------------------------------------------------------------------
; tools/fixtures/vga-test.asm
;
; Self-test boot sector for the ZenithOS headless emulator harness.
;
; What it proves:
;   1. The floppy image boots through SeaBIOS under v86.
;   2. VGA mode 13h (320x200x256) is set through the BIOS.
;   3. The harness can read back the emulated framebuffer as a PNG.
;   4. PS/2 keyboard scancodes injected by the harness reach the guest: every
;      key press paints one 16x16 bright block along the bottom of the screen.
;
; Screen layout:
;   rows   0..9   : 16 solid colour bars (20 px wide each, colours 0..15)
;   rows  10..199 : XOR gradient, colour = ((x>>3) ^ (y>>2)) & 0xFF
;   row   12      : white text "ZENITHOS VGA TEST OK" (BIOS teletype)
;   rows 176..191 : 16x16 blocks, one appended per key make-code seen
;
; Assemble:  nasm -f bin tools/fixtures/vga-test.asm -o tools/fixtures/vga-test.bin
; Build img: node tools/fixtures/build-fixture.mjs
; ---------------------------------------------------------------------------

bits 16
org 0x7c00

%define VIDEO_SEG   0xA000
%define SCREEN_W    320
%define SCREEN_H    200

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    ; --- VGA mode 13h: 320x200, 256 colours, linear framebuffer at 0xA0000 ---
    mov ax, 0x0013
    int 0x10

    ; --- fill framebuffer with XOR gradient ---------------------------------
    mov ax, VIDEO_SEG
    mov es, ax
    xor di, di                     ; destination offset = 0
    xor dx, dx                     ; y = 0
.y_loop:
    xor cx, cx                     ; x = 0
.x_loop:
    mov ax, cx
    shr ax, 3                      ; x >> 3
    mov bx, dx
    shr bx, 2                      ; y >> 2
    xor ax, bx
    stosb                          ; ES:DI <- AL, DI++
    inc cx
    cmp cx, SCREEN_W
    jb .x_loop
    inc dx
    cmp dx, SCREEN_H
    jb .y_loop

    ; --- 16 colour bars over the top 10 rows (20 px wide each) --------------
    xor di, di
    xor bx, bx
.bar_loop:
    mov al, bl
    mov cx, 10 * 20                ; 10 rows * 20 px, rows are contiguous
    rep stosb
    inc bx
    cmp bx, 16
    jb .bar_loop

    ; --- white text through BIOS teletype (proves VGA BIOS text output) ----
    mov ah, 0x02                   ; set cursor position
    xor bh, bh
    mov dh, 12                     ; row 12, below the colour bars
    mov dl, 10
    int 0x10

    mov si, msg
    mov ah, 0x0e                   ; teletype output
    mov bx, 0x000f                 ; page 0, bright white
.msg_loop:
    lodsb
    test al, al
    jz .msg_done
    int 0x10
    jmp .msg_loop
.msg_done:

    ; --- COM1 serial banner (proves --serial capture works) ----------------
    mov si, serial_msg
.serial_loop:
    lodsb
    test al, al
    jz .serial_done
    mov ah, al
.serial_wait:
    mov dx, 0x3FD                  ; COM1 line status register
    in al, dx
    test al, 0x20                  ; transmitter holding register empty?
    jz .serial_wait
    mov dx, 0x3F8                  ; COM1 data register
    mov al, ah
    out dx, al
    jmp .serial_loop
.serial_done:

    ; --- key-reactive blocks ------------------------------------------------
    ; Interrupts are disabled so the BIOS IRQ1 handler cannot steal the
    ; scancodes; the guest polls the 8042 output buffer directly.
    cli
    mov ax, VIDEO_SEG
    mov es, ax

    ; drain any stale bytes left in the keyboard controller
.drain:
    in al, 0x64
    test al, 1
    jz .drained
    in al, 0x60
    jmp .drain
.drained:

    xor bp, bp                     ; bp = number of keys seen
.poll:
    in al, 0x64
    test al, 1                     ; output buffer full?
    jz .poll
    in al, 0x60                    ; read scancode
    test al, 0x80                  ; ignore break codes
    jnz .poll

    inc bp
    ; x = ((bp-1) & 31) * 8
    mov ax, bp
    dec ax
    and ax, 0x1F
    shl ax, 3
    mov di, ax                     ; DI = x
    mov ax, 176
    mov cx, SCREEN_W
    mul cx                         ; DX:AX = 176*320
    add di, ax                     ; DI = y*320 + x

    mov ax, bp
    and al, 0x0F
    add al, 0x40                   ; bright colours 0x40..0x4F
    mov dx, 16                     ; 16 rows
.block_row:
    mov cx, 16                     ; 16 px per row
    push di
    rep stosb
    pop di
    add di, SCREEN_W
    dec dx
    jnz .block_row
    jmp .poll

msg: db 'ZENITHOS VGA TEST OK', 0
serial_msg: db 'ZENITHOS FIXTURE SERIAL OK', 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
