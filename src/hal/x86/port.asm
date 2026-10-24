; I/O port primitives (x86). cdecl ABI: [esp+4] = port, [esp+8] = value.
[BITS 32]
section .text

global outb
outb:
    mov dx, [esp+4]
    mov al, [esp+8]
    out dx, al
    ret

global inb
inb:
    mov dx, [esp+4]
    in al, dx
    movzx eax, al
    ret

global outw
outw:
    mov dx, [esp+4]
    mov ax, [esp+8]
    out dx, ax
    ret

global inw
inw:
    mov dx, [esp+4]
    in ax, dx
    movzx eax, ax
    ret

global outl
outl:
    mov dx, [esp+4]
    mov eax, [esp+8]
    out dx, eax
    ret

global inl
inl:
    mov dx, [esp+4]
    in eax, dx
    ret

global io_wait
io_wait:
    out 0x80, al
    ret
