; =============================================================================
;  ZenithOS v6.0 - boot/bios/boot.asm
;  BIOS loader: the 512-byte bootstrap of a 1.44 MiB raw floppy image.
;
;  Real mode:
;    1. set segments / stack, remember the boot drive
;    2. open A20 and verify it with the 1 MiB alias test
;    3. capture the BIOS E820 memory map into the BootInfo block at 0x0500
;    4. load the flat 32-bit kernel (KERNEL_SECTORS) to KERNEL_ORG over CHS
;    5. switch VGA to mode 13h while the BIOS still exists
;    6. write the BootInfo header (magic/version/count/drive/type/mode/flags)
;    7. install a flat 4 GiB GDT and enter protected mode, jump to the kernel
;
;  The kernel (C) owns the IDT, PIC, page tables and interrupts from here on.
;  Assemble: nasm -f bin -I src/include boot/bios/boot.asm -o boot.bin
; =============================================================================

[BITS 16]
[ORG 0x7C00]

%include "config.inc"

start:
    jmp short boot_main
    nop

boot_main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    mov [boot_drive], dl

    call enable_a20
    call e820_probe
    call load_kernel

    ; Graphics mode last: after this BIOS text output is gone.
    mov ax, 0x0013
    int 0x10

    ; Publish the BootInfo header for the kernel.
    mov dword [BOOTINFO_ADDR + 0], BOOTINFO_MAGIC
    mov word  [BOOTINFO_ADDR + 4], BOOTINFO_VERSION
    mov al, [boot_drive]
    mov [BOOTINFO_ADDR + 8], al       ; boot_drive
    mov byte [BOOTINFO_ADDR + 9], 0   ; boot_type = BIOS
    mov byte [BOOTINFO_ADDR + 10], 0x13 ; video_mode
    mov byte [BOOTINFO_ADDR + 11], 0x03 ; flags: VGA | keyboard present guess

    ; Enter protected mode with interrupts disabled.
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

; =============================================================================
;  A20 gate (clobbers AX, SI, DI, DS, ES)
; =============================================================================
enable_a20:
    call a20_check
    test al, al
    jnz .done
    mov ax, 0x2401
    int 0x15
    call a20_check
    test al, al
    jnz .done
    in al, 0x92
    or al, 0x02
    and al, 0xFE
    out 0x92, al
    call a20_check
    test al, al
    jz a20_error
.done:
    ret

; AL = 1 when physical 0x100500 is no longer an alias of 0x000500.
a20_check:
    xor ax, ax
    mov ds, ax
    mov si, 0x0500
    dec ax
    mov es, ax
    mov di, 0x0510
    mov bl, [ds:si]
    mov bh, [es:di]
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF
    cmp byte [ds:si], 0xFF
    mov al, 0
    je .restore
    mov al, 1
.restore:
    mov [es:di], bh
    mov [ds:si], bl
    ret

; =============================================================================
;  E820 physical-memory map -> BootInfo.entries (24-byte descriptors, max 24)
; =============================================================================
e820_probe:
    xor ebx, ebx
    xor bp, bp
    xor ax, ax
    mov es, ax
    mov di, BOOTINFO_ENTRIES
.loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    add di, 24
    inc bp
    cmp bp, BOOTINFO_MAX_E820
    jae .done
    test ebx, ebx
    jnz .loop
.done:
    mov [BOOTINFO_ADDR + 6], bp      ; e820_count (16-bit)
    ret

; =============================================================================
;  CHS kernel loader: LBA 1.. -> ES:BX = KERNEL_ORG (one sector per INT 13h)
; =============================================================================
load_kernel:
    mov byte [cyl], 0
    mov byte [head], 0
    mov byte [sector], 2
    mov word [dest_off], 0
    mov bp, KERNEL_SECTORS
.read:
    mov byte [retries], 3
.try:
    mov ax, KERNEL_ORG >> 4
    mov es, ax
    mov bx, [dest_off]
    mov ax, 0x0201
    mov ch, [cyl]
    mov cl, [sector]
    mov dh, [head]
    mov dl, [boot_drive]
    int 0x13
    jnc .ok
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec byte [retries]
    jnz .try
    jmp disk_error
.ok:
    add word [dest_off], 0x200
    inc byte [sector]
    cmp byte [sector], 19
    jb .advanced
    mov byte [sector], 1
    inc byte [head]
    cmp byte [head], 2
    jb .advanced
    mov byte [head], 0
    inc byte [cyl]
.advanced:
    dec bp
    jnz .read
    ret

; =============================================================================
;  BIOS text helpers + fatal errors
; =============================================================================
print:
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 0x0007
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    pop bx
    pop ax
    ret

disk_error:
    mov si, msg_disk
    call print
    jmp halt16

a20_error:
    mov si, msg_a20
    call print

halt16:
    cli
    hlt
    jmp halt16

; =============================================================================
;  Protected-mode trampoline
; =============================================================================
[BITS 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, KERNEL_STACK_TOP
    xor ebp, ebp
    cld
    mov ebx, BOOTINFO_ADDR
    mov eax, KERNEL_ORG
    jmp eax

; =============================================================================
;  GDT: null / flat 32-bit code (0x08) / flat 32-bit data (0x10)
; =============================================================================
align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; =============================================================================
;  State and strings
; =============================================================================
boot_drive   db 0
cyl          db 0
head         db 0
sector       db 0
retries      db 0
dest_off     dw 0

msg_disk db "DISK", 13, 10, 0
msg_a20  db "A20", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
