; =============================================================================
;  ZenithOS - lib/string.asm
;  Freestanding string/formatting helpers. No libc, no allocations.
;
;  Convenience: edi = destination, esi = source, ecx = count, eax = value.
;  All routines assume DF=0 (the kernel executes CLD on entry and never sets
;  DF). They clobber the registers they use; callers treat them as volatile.
; =============================================================================

[bits 32]

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

; -----------------------------------------------------------------------------
; str_len: ESI = NUL-terminated string -> EAX = length in bytes
; -----------------------------------------------------------------------------
str_len:
    xor eax, eax
.loop:
    cmp byte [esi + eax], 0
    je .done
    inc eax
    jmp .loop
.done:
    ret

; -----------------------------------------------------------------------------
; mem_copy: EDI = dst, ESI = src, ECX = count
; -----------------------------------------------------------------------------
mem_copy:
    cld
    rep movsb
    ret

; -----------------------------------------------------------------------------
; mem_set: EDI = dst, AL = byte, ECX = count
; -----------------------------------------------------------------------------
mem_set:
    cld
    rep stosb
    ret

; -----------------------------------------------------------------------------
; mem_zero: EDI = dst, ECX = count
; -----------------------------------------------------------------------------
mem_zero:
    cld
    xor eax, eax
    rep stosb
    ret

; -----------------------------------------------------------------------------
; u32_to_dec: EAX = value, EDI = buffer -> EAX = number of digits
; Writes a NUL terminator. Buffer must hold at least 11 bytes.
; -----------------------------------------------------------------------------
u32_to_dec:
    push ebx
    push ecx
    push edx
    mov ebx, 10
    xor ecx, ecx
.divide:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .divide
    mov eax, ecx
.emit:
    pop edx
    add dl, '0'
    mov [edi], dl
    inc edi
    dec ecx
    jnz .emit
    mov byte [edi], 0
    pop edx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; u32_to_dec_pad: EAX = value, EDI = buffer, ECX = minimum width -> EAX = length
; Zero-pads to the requested width (used for timestamps such as 00:07).
; -----------------------------------------------------------------------------
u32_to_dec_pad:
    push ebx
    push edx
    push esi
    push ebp
    mov esi, edi                ; destination start
    mov ebp, ecx                ; requested width
    mov ebx, 10
    sub esp, 16
    lea edi, [esp + 16]         ; build digits backwards
    xor ecx, ecx
.divide:
    xor edx, edx
    div ebx
    add dl, '0'
    dec edi
    mov [edi], dl
    inc ecx
    test eax, eax
    jnz .divide
    mov eax, ebp
    sub eax, ecx
    jle .no_pad
    mov edx, eax
.pad:
    mov byte [esi], '0'
    inc esi
    dec edx
    jnz .pad
    add ecx, eax
    mov ebx, ecx
    jmp .copy
.no_pad:
    mov ebx, ecx
.copy:
    mov edx, ecx
.copy_loop:
    mov al, [edi]
    mov [esi], al
    inc edi
    inc esi
    dec edx
    jnz .copy_loop
    mov byte [esi], 0
    add esp, 16
    mov eax, ebx
    pop ebp
    pop esi
    pop edx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; u32_to_hex: EAX = value, EDI = buffer -> writes 8 upper-case hex digits + NUL
; -----------------------------------------------------------------------------
u32_to_hex:
    push eax
    push ebx
    push ecx
    mov ebx, eax
    mov ecx, 8
.next:
    rol ebx, 4
    mov eax, ebx
    and eax, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .store
    add al, 7
.store:
    mov [edi], al
    inc edi
    dec ecx
    jnz .next
    mov byte [edi], 0
    pop ecx
    pop ebx
    pop eax
    ret
