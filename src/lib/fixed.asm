; =============================================================================
;  ZenithOS - lib/fixed.asm
;  Q8.8 fixed-point trigonometry used by the 3D neural renderer.
;
;  Angles are 16-bit "binary radians" (0x0000..0xFFFF = one turn).  The table
;  is indexed by the top 8 bits, so 256 angular steps per revolution.  Each
;  table entry is Q8.8 (-256..+256), generated at build time by build.js.
; =============================================================================

[bits 32]

%include "trig.inc"

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

; -----------------------------------------------------------------------------
; fsin: AX = 16-bit binary angle -> EAX = sin, Q8.8 signed
; -----------------------------------------------------------------------------
fsin:
    movzx eax, ah
    movsx eax, word [trig_sin + eax * 2]
    ret

; -----------------------------------------------------------------------------
; fcos: AX = 16-bit binary angle -> EAX = cos, Q8.8 signed
; -----------------------------------------------------------------------------
fcos:
    movzx eax, ah
    movsx eax, word [trig_cos + eax * 2]
    ret

; -----------------------------------------------------------------------------
; fmul_q8: EAX * EBX, both Q8.8 -> EAX = product Q8.8
; Both operands are small (coordinates and unit rotations), so the 32-bit
; product cannot overflow in practice.
; -----------------------------------------------------------------------------
fmul_q8:
    imul eax, ebx
    sar eax, 8
    ret

; -----------------------------------------------------------------------------
; sat_u8: clamp EAX to 0..255, result in EAX
; -----------------------------------------------------------------------------
sat_u8:
    test eax, eax
    jns .positive
    xor eax, eax
    ret
.positive:
    cmp eax, 255
    jbe .done
    mov eax, 255
.done:
    ret

; -----------------------------------------------------------------------------
; abs_i32: EAX = |EAX|
; -----------------------------------------------------------------------------
abs_i32:
    test eax, eax
    jns .done
    neg eax
.done:
    ret

; -----------------------------------------------------------------------------
; clamp_i32: EAX = clamp(EAX, EDX=min, ECX=max)
; -----------------------------------------------------------------------------
clamp_i32:
    cmp eax, edx
    jge .lower_ok
    mov eax, edx
.lower_ok:
    cmp eax, ecx
    jle .done
    mov eax, ecx
.done:
    ret
