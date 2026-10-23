; =============================================================================
;  ZenithOS - include/macros.inc
;  Small shared assembler helpers.  No code is emitted by this file.
; =============================================================================
%ifndef ZENITH_MACROS_INC
%define ZENITH_MACROS_INC

; Write an immediate/register to an I/O port.
%macro OUTB 2
    mov dx, %1
    mov al, %2
    out dx, al
%endmacro

; Read an I/O port into AL.
%macro INB 1
    mov dx, %1
    in al, dx
%endmacro

; Short I/O settling delay for the legacy PIC/PS2 devices.
%macro IO_DELAY 0
    nop
    nop
    nop
%endmacro

; Log-friendly unconditional breakpoint (used by self-tests).
%macro ASSERT_NOT_ZERO 1
    test %1, %1
    jnz %%ok
    int3
%%ok:
%endmacro

%endif
