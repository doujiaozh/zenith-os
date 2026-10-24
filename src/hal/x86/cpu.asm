; CPU control primitives (x86). cdecl ABI.
; Both symbol spellings exported (bare for TCC/Linux GCC, '_'-prefixed for
; mingw GCC on i386 PE); both resolve to the same code.
[BITS 32]
section .text

%macro cglobal 1
    global %1
    global _%1
    %1:
    _%1:
%endmacro

cglobal cpu_cli
    cli
    ret

cglobal cpu_sti
    sti
    ret

cglobal cpu_hlt
    hlt
    ret

cglobal cpu_rdtsc
    rdtsc                 ; -> edx:eax
    ret

cglobal cpu_read_cr0
    mov eax, cr0
    ret

cglobal cpu_read_cr2
    mov eax, cr2
    ret

cglobal cpu_read_cr3
    mov eax, cr3
    ret

cglobal cpu_write_cr0
    mov eax, [esp+4]
    mov cr0, eax
    ret

cglobal cpu_write_cr3
    mov eax, [esp+4]
    mov cr3, eax
    ret

cglobal cpu_invlpg
    mov eax, [esp+4]
    invlpg [eax]
    ret

cglobal cpu_lgdt
    mov eax, [esp+4]
    lgdt [eax]
    ret

cglobal cpu_lidt
    mov eax, [esp+4]
    lidt [eax]
    ret
