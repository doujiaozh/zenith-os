; =============================================================================
;  ZenithOS - mm/heap.asm
;  Kernel heap: an 8-byte-aligned first-fit free-list allocator over the
;  static arena HEAP_START..HEAP_END (~304 KiB).  Blocks carry a 4-byte
;  size|allocated flag followed by a next pointer; the allocated flag is bit 0
;  of the size (all sizes are multiples of 8).
;
;  This arena is deliberately separate from the PMM: the PMM owns physical
;  frames above 1 MiB for future userspace/driver use, while the heap gives the
;  kernel a zero-fragmentation-proof place for small objects today.
;
;  API
;    heap_init
;    kmalloc : EAX = bytes -> EAX = pointer (0 on OOM)
;    kfree   : EAX = pointer
;    heap_free_bytes  -> EAX
;    heap_used_bytes  -> EAX
; =============================================================================

[bits 32]

%include "include/config.inc"

%ifdef ZENITH_OBJ_CHECK
; No external dependencies.
%endif

HEAP_ALIGN   equ 8
HEAP_HDR     equ 8
MIN_SPLIT    equ 16                  ; header + minimum payload

; -----------------------------------------------------------------------------
; heap_init: one big free block
; -----------------------------------------------------------------------------
heap_init:
    mov eax, HEAP_END - HEAP_START - HEAP_HDR
    and eax, ~7
    mov dword [HEAP_START], eax       ; size, allocated bit clear
    mov dword [HEAP_START + 4], 0     ; next = null
    mov dword [heap_head], HEAP_START
    mov dword [heap_used], 0
    ret

; -----------------------------------------------------------------------------
; kmalloc: EAX = requested bytes
; -----------------------------------------------------------------------------
kmalloc:
    push ebx
    push ecx
    push edx
    push esi
    push edi

    test eax, eax
    jz .fail
    add eax, HEAP_ALIGN - 1
    and eax, ~(HEAP_ALIGN - 1)
    cmp eax, HEAP_ALIGN
    jae .size_ok
    mov eax, HEAP_ALIGN
.size_ok:
    mov edx, eax                      ; rounded size

    mov esi, [heap_head]
    xor edi, edi
.walk:
    test esi, esi
    jz .fail
    mov eax, [esi]
    test al, 1
    jnz .next
    cmp eax, edx
    jb .next

    mov ecx, eax
    sub ecx, edx                      ; leftover
    cmp ecx, MIN_SPLIT
    jb .whole

    ; Split: [ ESI | need | allocated ] [ new block | leftover-8 | free ]
    mov ebx, esi
    add ebx, HEAP_HDR
    add ebx, edx                      ; new block address
    mov eax, ecx
    sub eax, HEAP_HDR
    mov [ebx + 0], eax
    mov eax, [esi + 4]
    mov [ebx + 4], eax
    mov eax, edx
    or eax, 1
    mov [esi + 0], eax
    mov [esi + 4], ebx
    jmp .allocated

.whole:
    or eax, 1
    mov [esi + 0], eax

.allocated:
    add [heap_used], edx
    lea eax, [esi + HEAP_HDR]
    jmp .done

.next:
    mov edi, esi
    mov esi, [esi + 4]
    jmp .walk

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
; kfree: EAX = pointer returned by kmalloc
; -----------------------------------------------------------------------------
kfree:
    push ebx
    push ecx
    push edx
    push esi
    push edi

    test eax, eax
    jz .done
    sub eax, HEAP_HDR
    mov esi, eax

    ; Locate the block and its predecessor.
    mov edi, [heap_head]
    xor ebx, ebx
.find:
    cmp edi, esi
    je .found
    test edi, edi
    jz .done
    mov ebx, edi
    mov edi, [edi + 4]
    jmp .find

.found:
    mov eax, [esi]
    test al, 1
    jz .done                          ; not allocated / double free
    and eax, ~1
    mov [esi], eax
    sub [heap_used], eax

    ; Coalesce forward.
.forward:
    mov edx, [esi + 4]
    test edx, edx
    jz .backward
    mov ecx, [edx]
    test cl, 1
    jnz .backward
    lea eax, [esi + HEAP_HDR]
    add eax, [esi]
    cmp eax, edx
    jne .backward
    mov eax, [edx]
    and eax, ~1
    add eax, HEAP_HDR
    add [esi], eax
    mov eax, [edx + 4]
    mov [esi + 4], eax
    jmp .forward

    ; Coalesce backward.
.backward:
    test ebx, ebx
    jz .done
    mov ecx, [ebx]
    test cl, 1
    jnz .done
    lea eax, [ebx + HEAP_HDR]
    add eax, [ebx]
    cmp eax, esi
    jne .done
    mov eax, [esi]
    and eax, ~1
    add eax, HEAP_HDR
    add [ebx], eax
    mov eax, [esi + 4]
    mov [ebx + 4], eax
.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    ret

; -----------------------------------------------------------------------------
; heap_free_bytes: EAX = total free payload bytes
; -----------------------------------------------------------------------------
heap_free_bytes:
    push ebx
    mov esi, [heap_head]
    xor eax, eax
.walk:
    test esi, esi
    jz .done
    mov ebx, [esi]
    test bl, 1
    jnz .next
    and ebx, ~1
    add eax, ebx
.next:
    mov esi, [esi + 4]
    jmp .walk
.done:
    pop ebx
    ret

; -----------------------------------------------------------------------------
; heap_used_bytes: EAX = total allocated payload bytes
; -----------------------------------------------------------------------------
heap_used_bytes:
    mov eax, [heap_used]
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
align 4
heap_head dd 0
heap_used dd 0
