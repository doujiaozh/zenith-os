; =============================================================================
;  ZenithOS - mm/pmm.asm
;  Physical page-frame allocator: one bit per 4 KiB frame, indexed from the
;  BIOS E820 map captured by the boot sector.  Everything below 1 MiB is
;  permanently reserved (IVT/BDA, boot-info block, kernel image, VGA aperture,
;  back buffer, bitmap, heap arena, stack).
;
;  The bitmap lives at PMM_BITMAP_ADDR (16 KiB = 131,072 frames = 512 MiB).
;
;  API
;    pmm_init
;    pmm_alloc_page -> EAX = physical address of a free 4 KiB frame, or 0
;    pmm_free_page  : EAX = physical address
;    pmm_free_pages / pmm_total_pages / pmm_usable_kb : readable stats
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

ONE_MIB     equ 0x00100000
PAGE_SIZE   equ 4096
LOW_PAGE    equ ONE_MIB / PAGE_SIZE          ; 256

; -----------------------------------------------------------------------------
; pmm_init: mark everything used, then free every E820 type-1 frame >= 1 MiB.
; -----------------------------------------------------------------------------
pmm_init:
    pushad
    mov dword [pmm_total_pages], 0
    mov dword [pmm_free_pages], 0
    mov dword [pmm_usable_kb], 0
    mov dword [pmm_hint], LOW_PAGE

    ; All ones = every frame reserved.
    mov edi, PMM_BITMAP_ADDR
    mov ecx, PMM_BITMAP_BYTES / 4
    mov eax, 0xFFFFFFFF
    cld
    rep stosd

    cmp dword [BOOTINFO_ADDR], BOOTINFO_MAGIC
    jne .fallback
    movzx ebx, word [BOOTINFO_ADDR + 6]
    test ebx, ebx
    jz .fallback
    mov esi, BOOTINFO_ENTRIES

.entry_loop:
    cmp dword [esi + 16], 1          ; E820 type 1 = usable RAM
    jne .next_entry
    cmp dword [esi + 4], 0           ; ignore regions above 4 GiB
    jne .next_entry

    mov eax, [esi + 0]               ; base low
    mov edx, [esi + 8]               ; length low
    cmp dword [esi + 12], 0
    jne .cap_length
    add edx, eax
    jnc .after_add
    jmp .cap_end
.cap_length:
    mov edx, PMM_MAX_PAGES * PAGE_SIZE
    jmp .cap_end
.after_add:
    cmp edx, PMM_MAX_PAGES * PAGE_SIZE
    jbe .end_ok
.cap_end:
    mov edx, PMM_MAX_PAGES * PAGE_SIZE
.end_ok:
    cmp edx, ONE_MIB
    jbe .next_entry
    cmp eax, ONE_MIB
    jae .base_ok
    mov eax, ONE_MIB
.base_ok:
    add eax, PAGE_SIZE - 1
    shr eax, 12                      ; first page (rounded up)
    shr edx, 12                      ; last page (exclusive)
    cmp eax, edx
    jae .next_entry
    cmp edx, [pmm_total_pages]
    jbe .total_ok
    mov [pmm_total_pages], edx
.total_ok:
    ; Free [EAX, EDX)
.free_loop:
    mov ecx, eax
    shr ecx, 5
    mov edi, PMM_BITMAP_ADDR
    lea edi, [edi + ecx * 4]
    mov ecx, eax
    and ecx, 31
    btr dword [edi], ecx
    inc dword [pmm_free_pages]
    inc eax
    cmp eax, edx
    jb .free_loop

.next_entry:
    add esi, 24
    dec ebx
    jnz .entry_loop
    jmp .finish

.fallback:
    ; No usable map: assume a conservative 32 MiB of RAM.
    mov dword [pmm_total_pages], 8192
    mov eax, LOW_PAGE
.fallback_loop:
    mov ecx, eax
    shr ecx, 5
    mov edi, PMM_BITMAP_ADDR
    lea edi, [edi + ecx * 4]
    mov ecx, eax
    and ecx, 31
    btr dword [edi], ecx
    inc dword [pmm_free_pages]
    inc eax
    cmp eax, 8192
    jb .fallback_loop

.finish:
    mov eax, [pmm_free_pages]
    shl eax, 2                       ; KiB of free RAM
    mov [pmm_usable_kb], eax
    popad
    ret

; -----------------------------------------------------------------------------
; pmm_alloc_page: EAX = free frame address (0 on failure)
; -----------------------------------------------------------------------------
pmm_alloc_page:
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov ecx, [pmm_total_pages]
    cmp ecx, PMM_MAX_PAGES
    jbe .total_ok
    mov ecx, PMM_MAX_PAGES
.total_ok:
    test ecx, ecx
    jz .fail

    mov esi, [pmm_hint]
    cmp esi, ecx
    jb .hint_ok
    mov esi, LOW_PAGE
.hint_ok:
    mov edx, ecx
    call pmm_scan
    test eax, eax
    jnz .done

    mov esi, LOW_PAGE
    mov edx, [pmm_hint]
    cmp esi, edx
    jae .fail
    call pmm_scan
    jmp .done
.fail:
    xor eax, eax
.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; pmm_scan: ESI = first page, EDX = end page; EAX = address or 0
; -----------------------------------------------------------------------------
pmm_scan:
    push ebx
    push ecx
    push edi
    mov ecx, edx
    cmp esi, ecx
    jae .not_found
.loop:
    mov ebx, esi
    shr ebx, 5
    mov edi, PMM_BITMAP_ADDR
    lea edi, [edi + ebx * 4]
    mov ebx, esi
    and ebx, 31
    bt dword [edi], ebx
    jnc .found
    inc esi
    cmp esi, ecx
    jb .loop
.not_found:
    xor eax, eax
    jmp .out
.found:
    bts dword [edi], ebx
    dec dword [pmm_free_pages]
    lea eax, [esi + 1]
    mov [pmm_hint], eax
    shl esi, 12
    mov eax, esi
.out:
    pop edi
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; pmm_free_page: EAX = physical address
; -----------------------------------------------------------------------------
pmm_free_page:
    push ebx
    push edi
    test eax, eax
    jz .done
    shr eax, 12
    cmp eax, [pmm_total_pages]
    jae .done
    cmp eax, LOW_PAGE
    jb .done
    mov ebx, eax
    shr ebx, 5
    mov edi, PMM_BITMAP_ADDR
    lea edi, [edi + ebx * 4]
    mov ebx, eax
    and ebx, 31
    bt dword [edi], ebx
    jnc .done                        ; already free: ignore double free
    bts dword [edi], ebx
    inc dword [pmm_free_pages]
    cmp eax, [pmm_hint]
    jae .done
    mov [pmm_hint], eax
.done:
    pop edi
    pop ebx
    ret

; -----------------------------------------------------------------------------
; pmm_free_kb: EAX = free KiB
; -----------------------------------------------------------------------------
pmm_free_kb:
    mov eax, [pmm_free_pages]
    shl eax, 2
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
pmm_total_pages dd 0
pmm_free_pages  dd 0
pmm_usable_kb   dd 0
pmm_hint        dd 0
