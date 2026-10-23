; =============================================================================
;  ZenithOS - drivers/serial.asm
;  COM1 (16550-compatible) polled output. This is the other half of the
;  unified logger: every log line goes to the serial port *and* to the
;  in-memory ring buffer displayed by the HUD.
;
;  All transmits are timeout-bounded so a machine without a UART (or an
;  emulator with a minimal UART model) can never wedge the kernel.
; =============================================================================

[bits 32]

%include "include/config.inc"
%include "include/macros.inc"

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

; -----------------------------------------------------------------------------
; serial_init: 115200 8N1, FIFO enabled, no interrupts (polled output only)
; -----------------------------------------------------------------------------
serial_init:
    OUTB SERIAL_PORT + 1, 0x00      ; disable UART interrupts
    OUTB SERIAL_PORT + 3, 0x80      ; DLAB on
    OUTB SERIAL_PORT + 0, SERIAL_BAUD_DIV
    OUTB SERIAL_PORT + 1, 0x00
    OUTB SERIAL_PORT + 3, 0x03      ; 8 data bits, no parity, one stop bit
    OUTB SERIAL_PORT + 2, 0xC7      ; enable + clear FIFOs, 14-byte threshold
    OUTB SERIAL_PORT + 4, 0x0B      ; DTR + RTS + OUT2
    ret

; -----------------------------------------------------------------------------
; serial_putc: AL = character. Waits (bounded) for the transmit buffer.
; -----------------------------------------------------------------------------
serial_putc:
    push eax
    push ecx
    push edx
    mov ah, al
    mov ecx, 0x10000
.wait:
    mov dx, SERIAL_PORT + 5
    in al, dx
    test al, 0x20                   ; THR empty?
    jnz .send
    dec ecx
    jnz .wait
.send:
    mov dx, SERIAL_PORT
    mov al, ah
    out dx, al
    pop edx
    pop ecx
    pop eax
    ret

; -----------------------------------------------------------------------------
; serial_puts: ESI = NUL-terminated string
; -----------------------------------------------------------------------------
serial_puts:
    push eax
    push esi
.next:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp .next
.done:
    pop esi
    pop eax
    ret

; -----------------------------------------------------------------------------
; serial_printf_ready: returns AL=1 when the transmitter is empty
; -----------------------------------------------------------------------------
serial_tx_ready:
    mov dx, SERIAL_PORT + 5
    in al, dx
    and al, 0x20
    ret
