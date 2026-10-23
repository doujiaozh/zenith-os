; =============================================================================
;  ZenithOS - lib/selftest.asm
;  Ten boot-time self-tests.  The kernel refuses to claim success before these
;  pass; the result is logged through the unified logger, streamed to COM1 and
;  rendered in the HUD as "SELFTEST n/n".
;
;  Covered: boot-info hand-off, GDT, IDT, physical allocator, heap, spiking
;  network, CMOS RTC, PIT interrupt delivery, VGA back buffer, INT 0x80 ABI.
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
extern gdt_start
extern idt_table
extern pmm_free_pages
extern pmm_alloc_page
extern pmm_free_page
extern heap_free_bytes
extern kmalloc
extern kfree
extern snn_tick
extern snn_spike_count
extern snn_energy
extern pit_ticks
extern rtc_second
extern rtc_minute
extern rtc_hour
extern rtc_month
extern log_info
extern log_error
%endif

; -----------------------------------------------------------------------------
; selftest_run
; -----------------------------------------------------------------------------
selftest_run:
    pushad
    mov dword [selftest_passed], 0
    mov dword [selftest_total], 0
    mov esi, st_begin
    call log_info

    ; ---- 1. boot-info hand-off ---------------------------------------------
    xor bl, bl
    cmp dword [BOOTINFO_ADDR], BOOTINFO_MAGIC
    jne .t1_done
    cmp word [BOOTINFO_ADDR + 4], BOOTINFO_VERSION
    jne .t1_done
    mov bl, 1
.t1_done:
    mov esi, st_bootinfo
    call selftest_report

    ; ---- 2. GDT loaded ------------------------------------------------------
    sgdt [st_gdtr]
    xor bl, bl
    cmp dword [st_gdtr + 2], gdt_start
    jne .t2_done
    cmp word [st_gdtr], 23
    jne .t2_done
    mov bl, 1
.t2_done:
    mov esi, st_gdt
    call selftest_report

    ; ---- 3. IDT loaded ------------------------------------------------------
    sidt [st_idtr]
    xor bl, bl
    cmp dword [st_idtr + 2], idt_table
    jne .t3_done
    cmp word [st_idtr], 2047
    jne .t3_done
    mov bl, 1
.t3_done:
    mov esi, st_idt
    call selftest_report

    ; ---- 4. physical page allocator -----------------------------------------
    xor bl, bl
    mov eax, [pmm_free_pages]
    mov [st_pmm_free], eax
    call pmm_alloc_page
    test eax, eax
    jz .t4_done
    mov [st_page1], eax
    call pmm_alloc_page
    test eax, eax
    jz .t4_free1
    cmp eax, [st_page1]
    je .t4_free2
    mov [st_page2], eax
    mov eax, [st_page1]
    call pmm_free_page
    mov eax, [st_page2]
    call pmm_free_page
    mov eax, [pmm_free_pages]
    cmp eax, [st_pmm_free]
    jne .t4_done
    mov bl, 1
    jmp .t4_done
.t4_free2:
    mov eax, [st_page2]
    call pmm_free_page
.t4_free1:
    mov eax, [st_page1]
    call pmm_free_page
.t4_done:
    mov esi, st_pmm
    call selftest_report

    ; ---- 5. kernel heap ------------------------------------------------------
    xor bl, bl
    call heap_free_bytes
    mov [st_heap_free], eax
    mov eax, 64
    call kmalloc
    test eax, eax
    jz .t5_done
    mov [st_ptr], eax
    test eax, 7
    jnz .t5_free
    cmp eax, HEAP_START
    jb .t5_free
    cmp eax, HEAP_END
    jae .t5_free
    mov byte [eax], 0x5A
    mov byte [eax + 63], 0xA5
    cmp byte [eax], 0x5A
    jne .t5_free
    cmp byte [eax + 63], 0xA5
    jne .t5_free
    mov bl, 1
.t5_free:
    mov eax, [st_ptr]
    call kfree
    call heap_free_bytes
    cmp eax, [st_heap_free]
    je .t5_done
    xor bl, bl
.t5_done:
    mov esi, st_heap
    call selftest_report

    ; ---- 6. spiking neural network ------------------------------------------
    xor bl, bl
    mov eax, [snn_spike_count]
    mov [st_spike], eax
    mov ecx, 48
.t6_loop:
    push ecx
    call snn_tick
    pop ecx
    loop .t6_loop
    mov eax, [snn_spike_count]
    cmp eax, [st_spike]
    jbe .t6_done
    mov eax, [snn_energy]
    cmp eax, SNN_NEURONS * 255
    ja .t6_done
    mov bl, 1
.t6_done:
    mov esi, st_snn
    call selftest_report

    ; ---- 7. CMOS real-time clock --------------------------------------------
    xor bl, bl
    mov al, [rtc_second]
    cmp al, 59
    ja .t7_done
    mov al, [rtc_minute]
    cmp al, 59
    ja .t7_done
    mov al, [rtc_hour]
    cmp al, 23
    ja .t7_done
    mov al, [rtc_month]
    test al, al
    jz .t7_done
    cmp al, 12
    ja .t7_done
    mov bl, 1
.t7_done:
    mov esi, st_rtc
    call selftest_report

    ; ---- 8. timer interrupts actually arrive --------------------------------
    xor bl, bl
    mov eax, [pit_ticks]
    mov [st_ticks], eax
    mov ecx, 300000
.t8_spin:
    dec ecx
    jnz .t8_spin
    mov eax, [pit_ticks]
    cmp eax, [st_ticks]
    jbe .t8_done
    mov bl, 1
.t8_done:
    mov esi, st_pit
    call selftest_report

    ; ---- 9. VGA back buffer is writable -------------------------------------
    xor bl, bl
    mov edi, VGA_BACKBUF
    mov al, [edi]
    mov [st_pixel], al
    mov byte [edi], 0x5A
    cmp byte [edi], 0x5A
    jne .t9_done
    mov al, [st_pixel]
    mov [edi], al
    mov bl, 1
.t9_done:
    mov esi, st_vga
    call selftest_report

    ; ---- 10. INT 0x80 kernel-call ABI ---------------------------------------
    xor bl, bl
    mov eax, 0
    int 0x80
    test eax, eax
    jz .t10_done
    mov bl, 1
.t10_done:
    mov esi, st_syscall
    call selftest_report

    mov esi, st_done
    call log_info
    popad
    ret

; -----------------------------------------------------------------------------
; selftest_report: ESI = test name, BL = 1 pass / 0 fail
; -----------------------------------------------------------------------------
selftest_report:
    inc dword [selftest_total]
    test bl, bl
    jz .fail
    inc dword [selftest_passed]
    call log_info
    ret
.fail:
    call log_error
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
selftest_passed dd 0
selftest_total  dd 0
st_gdtr         times 8 db 0
st_idtr         times 8 db 0
st_pmm_free     dd 0
st_heap_free    dd 0
st_spike        dd 0
st_ticks        dd 0
st_page1        dd 0
st_page2        dd 0
st_ptr          dd 0
st_pixel        db 0

st_begin     db "selftest: starting 10 checks", 0
st_bootinfo  db "selftest: boot-info hand-off", 0
st_gdt       db "selftest: GDT reloaded", 0
st_idt       db "selftest: IDT installed", 0
st_pmm       db "selftest: physical page allocator", 0
st_heap      db "selftest: kernel heap alloc/free", 0
st_snn       db "selftest: spiking neural network", 0
st_rtc       db "selftest: CMOS real-time clock", 0
st_pit       db "selftest: PIT interrupt delivery", 0
st_vga       db "selftest: VGA back buffer", 0
st_syscall   db "selftest: INT 0x80 call ABI", 0
st_done      db "selftest: complete", 0
