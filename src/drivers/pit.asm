; =============================================================================
;  ZenithOS - drivers/pit.asm
;  8254 programmable interval timer, channel 0, 100 Hz system heartbeat.
;  The tick counter is the kernel's only clock source: the renderer paces
;  frames with it, the SNN advances with it and the HUD derives FPS/uptime.
; =============================================================================

[bits 32]

%include "include/config.inc"
%include "include/macros.inc"

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

; -----------------------------------------------------------------------------
; pit_init: channel 0, mode 2 (rate generator), 100 Hz
; -----------------------------------------------------------------------------
pit_init:
    mov al, 0x36                    ; channel 0, lobyte/hibyte, mode 3
    out 0x43, al
    mov ax, PIT_BASE / PIT_HZ       ; 11932
    out 0x40, al
    mov al, ah
    out 0x40, al
    mov dword [pit_ticks], 0
    ret

; -----------------------------------------------------------------------------
; pit_irq_handler: IRQ0. Keep it minimal: one increment, then return.
; -----------------------------------------------------------------------------
pit_irq_handler:
    inc dword [pit_ticks]
    ret

; -----------------------------------------------------------------------------
; pit_uptime_ms: EAX = milliseconds since boot
; -----------------------------------------------------------------------------
pit_uptime_ms:
    mov eax, [pit_ticks]
    imul eax, 1000 / PIT_HZ
    ret

align 4
pit_ticks dd 0
