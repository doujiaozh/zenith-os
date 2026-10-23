; =============================================================================
;  ZenithOS - drivers/rtc.asm
;  CMOS real-time clock (ports 0x70/0x71).  The kernel only reads wall-clock
;  time for the HUD; it never writes the RTC, so a broken/absent RTC cannot
;  affect the boot.  Both BCD and binary modes and both 12h/24h modes are
;  normalised to BCD-decoded 24-hour fields.
; =============================================================================

[bits 32]

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

; -----------------------------------------------------------------------------
; rtc_read_reg: AL = register index -> AL = value
; -----------------------------------------------------------------------------
rtc_read_reg:
    out 0x70, al
    in al, 0x71
    ret

; -----------------------------------------------------------------------------
; rtc_init: read status register B and the current time
; -----------------------------------------------------------------------------
rtc_init:
    pushad
    mov al, 0x0B
    call rtc_read_reg
    mov [rtc_status_b], al
    call rtc_read_time
    popad
    ret

; -----------------------------------------------------------------------------
; rtc_read_time: refresh rtc_second/minute/hour/day/month/year (binary, 24h)
; -----------------------------------------------------------------------------
rtc_read_time:
    pushad

    ; Wait for the update-in-progress flag to clear (bounded).
    mov ecx, 0x10000
.wait:
    mov al, 0x0A
    call rtc_read_reg
    test al, 0x80
    jz .ready
    loop .wait
.ready:

    mov al, 0x00
    call rtc_read_reg
    mov [rtc_second], al
    mov al, 0x02
    call rtc_read_reg
    mov [rtc_minute], al
    mov al, 0x04
    call rtc_read_reg
    mov [rtc_hour], al
    mov al, 0x07
    call rtc_read_reg
    mov [rtc_day], al
    mov al, 0x08
    call rtc_read_reg
    mov [rtc_month], al
    mov al, 0x09
    call rtc_read_reg
    mov [rtc_year], al

    ; BCD -> binary unless status B bit 2 says the RTC is already binary.
    test byte [rtc_status_b], 0x04
    jnz .binary
    mov al, [rtc_second]
    call rtc_bcd_to_bin
    mov [rtc_second], al
    mov al, [rtc_minute]
    call rtc_bcd_to_bin
    mov [rtc_minute], al
    mov al, [rtc_hour]
    call rtc_bcd_to_bin
    mov [rtc_hour], al
    mov al, [rtc_day]
    call rtc_bcd_to_bin
    mov [rtc_day], al
    mov al, [rtc_month]
    call rtc_bcd_to_bin
    mov [rtc_month], al
    mov al, [rtc_year]
    call rtc_bcd_to_bin
    mov [rtc_year], al

.binary:
    ; 12-hour clock? Normalise to 24-hour.
    test byte [rtc_status_b], 0x02
    jnz .done
    mov al, [rtc_hour]
    test al, 0x80                   ; bit 7 = PM in 12h mode
    jz .am
    and al, 0x7F
    cmp al, 12
    jae .pm_store
    add al, 12
.pm_store:
    mov [rtc_hour], al
    jmp .done
.am:
    cmp al, 12
    jne .done
    mov byte [rtc_hour], 0
.done:
    popad
    ret

; -----------------------------------------------------------------------------
; rtc_bcd_to_bin: AL = packed BCD -> AL = binary
; -----------------------------------------------------------------------------
rtc_bcd_to_bin:
    push ebx
    movzx ebx, al
    and al, 0x0F
    shr ebx, 4
    imul ebx, ebx, 10
    add al, bl
    pop ebx
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
rtc_status_b db 0
rtc_second   db 0
rtc_minute   db 0
rtc_hour     db 0
rtc_day      db 0
rtc_month    db 0
rtc_year     db 0
