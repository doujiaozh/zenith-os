; =============================================================================
;  ZenithOS - drivers/vga.asm
;  VGA mode 13h (320x200x256) driver with an off-screen back buffer.
;
;  The BIOS already switched the display to mode 13h during boot, so the kernel
;  never needs real-mode code: it only owns the palette (DAC), the 64,000-byte
;  back buffer at VGA_BACKBUF, and the copy to the 0xA0000 aperture.
;
;  Drawing API (all coordinates are signed pixels, clipped to the framebuffer):
;    vga_init
;    vga_clear          : EAX = colour
;    vga_clear_dither   : EAX = row colour A, EDX = row colour B
;    vga_putpixel       : EAX = x, EBX = y, ECX = colour
;    vga_hline          : EAX = x, EBX = y, ECX = length, EDX = colour
;    vga_vline          : EAX = x, EBX = y, ECX = length, EDX = colour
;    vga_fill_rect      : EAX = x, EBX = y, ECX = w, EDX = h, ESI = colour
;    vga_line           : EAX = x0, EBX = y0, ECX = x1, EDX = y1, ESI = colour
;    vga_draw_char      : EAX = x, EBX = y, ECX = char, EDX = colour
;    vga_draw_char_big  : EAX = x, EBX = y, ECX = char, EDX = colour (2x)
;    vga_draw_text      : EAX = x, EBX = y, ESI = string, EDX = colour
;    vga_draw_text_big  : EAX = x, EBX = y, ESI = string, EDX = colour (2x)
;    vga_set_palette    : ESI = RGB triples, EDX = first index, ECX = count
;    vga_present        : back buffer -> 0xA0000
;    vga_wait_vsync     : bounded poll of the vertical-retrace bit
;
;  Text uses src/ui/font.asm: 5x7 glyphs, 5 bytes each, bit0 = top row.
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
extern font_5x7
%endif

; -----------------------------------------------------------------------------
; vga_init: clear the back buffer and present a blank screen.
; The palette is installed by ui/theme.asm before the first real frame.
; -----------------------------------------------------------------------------
vga_init:
    xor eax, eax
    call vga_clear
    call vga_present
    ret

; -----------------------------------------------------------------------------
; vga_clear: EAX = colour
; -----------------------------------------------------------------------------
vga_clear:
    movzx eax, al
    imul eax, 0x01010101
    mov edi, VGA_BACKBUF
    mov ecx, VGA_BACKBUF_BYTES / 4
    cld
    rep stosd
    ret

; -----------------------------------------------------------------------------
; vga_clear_dither: EAX = colour for even rows, EDX = colour for odd rows
; Gives the background a subtle CRT scanline texture for free.
; -----------------------------------------------------------------------------
vga_clear_dither:
    movzx eax, al
    imul eax, 0x01010101
    movzx edx, dl
    imul edx, 0x01010101
    mov esi, eax
    mov edi, VGA_BACKBUF
    mov ecx, VGA_HEIGHT / 2
.loop:
    push ecx
    mov eax, esi
    mov ecx, VGA_WIDTH / 4
    rep stosd
    mov eax, edx
    mov ecx, VGA_WIDTH / 4
    rep stosd
    pop ecx
    dec ecx
    jnz .loop
    ret

; -----------------------------------------------------------------------------
; vga_putpixel: EAX = x, EBX = y, ECX = colour
; -----------------------------------------------------------------------------
vga_putpixel:
    cmp eax, VGA_WIDTH
    jae .done
    cmp ebx, VGA_HEIGHT
    jae .done
    imul edi, ebx, VGA_WIDTH
    add edi, eax
    add edi, VGA_BACKBUF
    mov [edi], cl
.done:
    ret

; -----------------------------------------------------------------------------
; vga_hline: EAX = x, EBX = y, ECX = length, EDX = colour
; -----------------------------------------------------------------------------
vga_hline:
    test ecx, ecx
    jle .done
    cmp ebx, VGA_HEIGHT
    jae .done
    test eax, eax
    jns .x_ok
    add ecx, eax
    jle .done
    xor eax, eax
.x_ok:
    cmp eax, VGA_WIDTH
    jae .done
    mov esi, VGA_WIDTH
    sub esi, eax
    cmp ecx, esi
    jbe .len_ok
    mov ecx, esi
.len_ok:
    imul edi, ebx, VGA_WIDTH
    add edi, eax
    add edi, VGA_BACKBUF
    mov eax, edx
    cld
    rep stosb
.done:
    ret

; -----------------------------------------------------------------------------
; vga_vline: EAX = x, EBX = y, ECX = length, EDX = colour
; -----------------------------------------------------------------------------
vga_vline:
    test ecx, ecx
    jle .done
    cmp eax, VGA_WIDTH
    jae .done
    test ebx, ebx
    jns .y_ok
    add ecx, ebx
    jle .done
    xor ebx, ebx
.y_ok:
    cmp ebx, VGA_HEIGHT
    jae .done
    mov esi, VGA_HEIGHT
    sub esi, ebx
    cmp ecx, esi
    jbe .len_ok
    mov ecx, esi
.len_ok:
    imul edi, ebx, VGA_WIDTH
    add edi, eax
    add edi, VGA_BACKBUF
.loop:
    mov [edi], dl
    add edi, VGA_WIDTH
    dec ecx
    jnz .loop
.done:
    ret

; -----------------------------------------------------------------------------
; vga_fill_rect: EAX = x, EBX = y, ECX = w, EDX = h, ESI = colour
; -----------------------------------------------------------------------------
vga_fill_rect:
    push ebp
    mov ebp, esi                    ; colour
    test ecx, ecx
    jle .done
    test edx, edx
    jle .done
    cmp ebx, VGA_HEIGHT
    jae .done
    cmp eax, VGA_WIDTH
    jae .done
    test ebx, ebx
    jns .y_ok
    add edx, ebx
    jle .done
    xor ebx, ebx
.y_ok:
    test eax, eax
    jns .x_ok
    add ecx, eax
    jle .done
    xor eax, eax
.x_ok:
    mov esi, VGA_WIDTH
    sub esi, eax
    cmp ecx, esi
    jbe .w_ok
    mov ecx, esi
.w_ok:
    mov esi, VGA_HEIGHT
    sub esi, ebx
    cmp edx, esi
    jbe .h_ok
    mov edx, esi
.h_ok:
    imul edi, ebx, VGA_WIDTH
    add edi, eax
    add edi, VGA_BACKBUF
    mov esi, VGA_WIDTH
    sub esi, ecx                    ; row stride remainder
.row:
    push ecx
    mov eax, ebp
    cld
    rep stosb
    pop ecx
    add edi, esi
    dec edx
    jnz .row
.done:
    pop ebp
    ret

; -----------------------------------------------------------------------------
; vga_line: EAX = x0, EBX = y0, ECX = x1, EDX = y1, ESI = colour
; Integer Bresenham.  Clipped per-pixel; the renderer pre-culls grossly
; off-screen edges so this stays cheap.
; -----------------------------------------------------------------------------
vga_line:
    mov [line_color], esi
    mov [line_x1], ecx
    mov [line_y1], edx

    mov esi, ecx
    sub esi, eax
    mov dword [line_sx], 1
    test esi, esi
    jns .x_pos
    neg esi
    mov dword [line_sx], -1
.x_pos:
    mov ecx, esi                    ; dx

    mov edi, edx
    sub edi, ebx
    mov dword [line_sy], 1
    test edi, edi
    jns .y_pos
    neg edi
    mov dword [line_sy], -1
.y_pos:
    neg edi
    mov edx, edi                    ; dy (negative)
    mov ebp, ecx
    add ebp, edx                    ; err = dx + dy

    mov dword [line_iters], 0
.loop:
    inc dword [line_iters]
    cmp dword [line_iters], 4096    ; hard guard: never wedge the desktop
    ja .done
    cmp eax, VGA_WIDTH
    jae .skip_plot
    cmp ebx, VGA_HEIGHT
    jae .skip_plot
    imul edi, ebx, VGA_WIDTH
    add edi, eax
    push eax
    mov al, [line_color]
    mov [edi + VGA_BACKBUF], al
    pop eax
.skip_plot:
    cmp eax, [line_x1]
    jne .step
    cmp ebx, [line_y1]
    je .done
.step:
    mov edi, ebp
    add edi, edi                    ; e2
    cmp edi, edx
    jl .no_x
    add ebp, edx
    add eax, [line_sx]
.no_x:
    cmp edi, ecx
    jg .no_y
    add ebp, ecx
    add ebx, [line_sy]
.no_y:
    jmp .loop
.done:
    ret

; -----------------------------------------------------------------------------
; vga_draw_char: EAX = x, EBX = y, ECX = ASCII, EDX = colour
; -----------------------------------------------------------------------------
vga_draw_char:
    mov [char_x], eax
    mov [char_y], ebx
    mov [char_color], edx
    mov eax, ecx
    sub eax, 32
    js .done
    cmp eax, 94
    ja .done
    imul eax, 5
    lea esi, [font_5x7 + eax]
    xor ecx, ecx
.col:
    movzx edx, byte [esi + ecx]
    test edx, edx
    jz .next_col
    mov eax, [char_x]
    add eax, ecx
    cmp eax, VGA_WIDTH
    jae .next_col
    mov ebx, [char_y]
    cmp ebx, VGA_HEIGHT
    jae .next_col
    imul edi, ebx, VGA_WIDTH
    add edi, eax
    add edi, VGA_BACKBUF
    mov ebp, VGA_HEIGHT
    sub ebp, ebx                    ; rows still visible (EBX is still y here)
    mov bl, [char_color]
    test dl, 0x01
    jz .r1
    cmp ebp, 1
    jb .next_col
    mov [edi + 0], bl
.r1:
    test dl, 0x02
    jz .r2
    cmp ebp, 2
    jb .next_col
    mov [edi + 320], bl
.r2:
    test dl, 0x04
    jz .r3
    cmp ebp, 3
    jb .next_col
    mov [edi + 640], bl
.r3:
    test dl, 0x08
    jz .r4
    cmp ebp, 4
    jb .next_col
    mov [edi + 960], bl
.r4:
    test dl, 0x10
    jz .r5
    cmp ebp, 5
    jb .next_col
    mov [edi + 1280], bl
.r5:
    test dl, 0x20
    jz .r6
    cmp ebp, 6
    jb .next_col
    mov [edi + 1600], bl
.r6:
    test dl, 0x40
    jz .next_col
    cmp ebp, 7
    jb .next_col
    mov [edi + 1920], bl
.next_col:
    inc ecx
    cmp ecx, 5
    jb .col
.done:
    ret

; -----------------------------------------------------------------------------
; vga_draw_char_big: EAX = x, EBX = y, ECX = ASCII, EDX = colour, 2x scale
; -----------------------------------------------------------------------------
vga_draw_char_big:
    mov [big_x], eax
    mov [big_y], ebx
    mov [big_color], edx
    mov eax, ecx
    sub eax, 32
    js .done
    cmp eax, 94
    ja .done
    imul eax, 5
    lea esi, [font_5x7 + eax]
    xor ecx, ecx
.col:
    movzx edx, byte [esi + ecx]
    test edx, edx
    jz .next_col
    xor ebp, ebp
.row:
    test dl, 1
    jz .next_row
    mov eax, [big_x]
    lea eax, [eax + ecx * 2]
    mov ebx, [big_y]
    lea ebx, [ebx + ebp * 2]
    cmp eax, VGA_WIDTH - 1
    jae .next_row
    cmp ebx, VGA_HEIGHT - 1
    jae .next_row
    imul edi, ebx, VGA_WIDTH
    add edi, eax
    add edi, VGA_BACKBUF
    mov al, [big_color]
    mov [edi + 0], al
    mov [edi + 1], al
    mov [edi + 320], al
    mov [edi + 321], al
.next_row:
    shr edx, 1
    inc ebp
    cmp ebp, 7
    jb .row
.next_col:
    inc ecx
    cmp ecx, 5
    jb .col
.done:
    ret

; -----------------------------------------------------------------------------
; vga_draw_text: EAX = x, EBX = y, ESI = NUL-terminated string, EDX = colour
; -----------------------------------------------------------------------------
vga_draw_text:
    mov [text_x], eax
    mov [text_y], ebx
    mov [text_color], edx
    mov [text_str], esi
.loop:
    mov esi, [text_str]
    lodsb
    mov [text_str], esi
    test al, al
    jz .done
    movzx ecx, al
    mov eax, [text_x]
    mov ebx, [text_y]
    mov edx, [text_color]
    call vga_draw_char
    add dword [text_x], 6
    jmp .loop
.done:
    ret

; -----------------------------------------------------------------------------
; vga_draw_text_big: 2x scale version of vga_draw_text
; -----------------------------------------------------------------------------
vga_draw_text_big:
    mov [text_x], eax
    mov [text_y], ebx
    mov [text_color], edx
    mov [text_str], esi
.loop:
    mov esi, [text_str]
    lodsb
    mov [text_str], esi
    test al, al
    jz .done
    movzx ecx, al
    mov eax, [text_x]
    mov ebx, [text_y]
    mov edx, [text_color]
    call vga_draw_char_big
    add dword [text_x], 12
    jmp .loop
.done:
    ret

; -----------------------------------------------------------------------------
; vga_set_palette: ESI = pointer to RGB triplets (0..63 per channel),
;                  EDX = first palette index, ECX = number of entries
; -----------------------------------------------------------------------------
vga_set_palette:
    push eax
    push edx
    mov eax, edx
    mov dx, VGA_DAC_WRITE
    out dx, al
    inc dx                          ; 0x3C9 data port
    imul ecx, 3
.loop:
    lodsb
    out dx, al
    dec ecx
    jnz .loop
    pop edx
    pop eax
    ret

; -----------------------------------------------------------------------------
; vga_present: copy the back buffer to the VGA aperture
; -----------------------------------------------------------------------------
vga_present:
    cld
    mov esi, VGA_BACKBUF
    mov edi, VGA_FB
    mov ecx, VGA_BACKBUF_BYTES / 4
    rep movsd
    ret

; -----------------------------------------------------------------------------
; vga_wait_vsync: bounded vertical-retrace poll (never hangs on odd hardware)
; -----------------------------------------------------------------------------
vga_wait_vsync:
    mov ecx, 0x20000
    mov dx, VGA_INPUT_STATUS
.wait_low:
    in al, dx
    test al, 0x08
    jnz .wait_high
    dec ecx
    jnz .wait_low
    ret
.wait_high:
    mov ecx, 0x20000
.high_loop:
    in al, dx
    test al, 0x08
    jz .done
    dec ecx
    jnz .high_loop
.done:
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
line_color   dd 0
line_x1      dd 0
line_y1      dd 0
line_sx      dd 0
line_sy      dd 0
line_iters   dd 0
char_x       dd 0
char_y       dd 0
char_color   dd 0
big_x        dd 0
big_y        dd 0
big_color    dd 0
text_x       dd 0
text_y       dd 0
text_color   dd 0
text_str     dd 0
