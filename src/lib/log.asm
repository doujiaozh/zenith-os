; =============================================================================
;  ZenithOS - lib/log.asm
;  Unified logging: every event is timestamped, sent to COM1 and appended to a
;  32-entry lock-free-ish ring buffer. The HUD renders the newest entry and
;  the self-tests report through the same path, so screen, serial and CI logs
;  never disagree.
;
;  API
;    log_info / log_warn / log_error : ESI = message
;    log_str                         : EAX = level, ESI = message
;    log_last_msg / log_last_level / log_count / log_error_count : readable state
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
extern serial_puts
extern pit_ticks
%endif

LOG_RING_ENTRIES equ 32
LOG_ENTRY_BYTES  equ 12          ; {message ptr, tick, level}

; -----------------------------------------------------------------------------
; log_init: arm the logger with a boot banner
; -----------------------------------------------------------------------------
log_init:
    mov dword [log_head], 0
    mov dword [log_count], 0
    mov dword [log_error_count], 0
    mov dword [log_last_level], 0
    mov esi, msg_log_boot
    call log_info
    ret

; -----------------------------------------------------------------------------
; log_info / log_warn / log_error: ESI = message
; -----------------------------------------------------------------------------
log_info:
    xor eax, eax
    jmp log_str

log_warn:
    mov eax, 1
    jmp log_str

log_error:
    mov eax, 2
    jmp log_str

; -----------------------------------------------------------------------------
; log_str: EAX = level (0 info, 1 warn, 2 error), ESI = NUL-terminated message
; -----------------------------------------------------------------------------
log_str:
    pushad
    mov ebx, eax

    ; Remember the newest entry for the HUD.
    mov [log_last_msg], esi
    mov [log_last_level], ebx

    ; Append to the ring buffer.
    mov ecx, [log_head]
    imul edx, ecx, LOG_ENTRY_BYTES
    lea edi, [log_ring + edx]
    mov [edi + 0], esi
    mov eax, [pit_ticks]
    mov [edi + 4], eax
    mov [edi + 8], ebx
    inc ecx
    and ecx, LOG_RING_ENTRIES - 1
    mov [log_head], ecx
    cmp dword [log_count], LOG_RING_ENTRIES
    jae .ring_full
    inc dword [log_count]
.ring_full:

    ; Errors get counted for the HUD status line.
    cmp ebx, 2
    jne .emit
    inc dword [log_error_count]

.emit:
    cmp ebx, 2
    je .prefix_error
    cmp ebx, 1
    je .prefix_warn
    mov esi, log_prefix_info
    jmp .prefix_done
.prefix_warn:
    mov esi, log_prefix_warn
    jmp .prefix_done
.prefix_error:
    mov esi, log_prefix_error
.prefix_done:
    call serial_puts
    mov esi, [log_last_msg]
    call serial_puts
    mov esi, log_crlf
    call serial_puts
    popad
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
log_prefix_info  db "[INFO ] ", 0
log_prefix_warn  db "[WARN ] ", 0
log_prefix_error db "[ERROR] ", 0
log_crlf         db 13, 10, 0

msg_log_boot     db "logger online: unified serial + ring buffer", 0

align 4
log_head         dd 0
log_count        dd 0
log_error_count  dd 0
log_last_msg     dd 0
log_last_level   dd 0
log_ring         times LOG_RING_ENTRIES * LOG_ENTRY_BYTES db 0
