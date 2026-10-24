; I/O port primitives (x86). cdecl ABI: [esp+4] = port, [esp+8] = value.
; Export both the bare and the underscored name: TCC and Linux GCC reference
; the bare symbol, mingw GCC adds a leading '_' on i386 PE.  Both land on the
; same code, so any compiler + platform combination links.
[BITS 32]
section .text

%macro cglobal 1
    global %1
    global _%1
    %1:
    _%1:
%endmacro

cglobal outb
    mov dx, [esp+4]
    mov al, [esp+8]
    out dx, al
    ret

cglobal inb
    mov dx, [esp+4]
    in al, dx
    movzx eax, al
    ret

cglobal outw
    mov dx, [esp+4]
    mov ax, [esp+8]
    out dx, ax
    ret

cglobal inw
    mov dx, [esp+4]
    in ax, dx
    movzx eax, ax
    ret

cglobal outl
    mov dx, [esp+4]
    mov eax, [esp+8]
    out dx, eax
    ret

cglobal inl
    mov dx, [esp+4]
    in eax, dx
    ret

cglobal io_wait
    out 0x80, al
    ret
