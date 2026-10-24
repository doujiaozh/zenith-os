; CPU control primitives (x86). cdecl ABI.
[BITS 32]
section .text

global cpu_cli
cpu_cli:
    cli
    ret

global cpu_sti
cpu_sti:
    sti
    ret

global cpu_hlt
cpu_hlt:
    hlt
    ret

global cpu_rdtsc
cpu_rdtsc:
    rdtsc                 ; -> edx:eax
    ret

global cpu_read_cr0
cpu_read_cr0:
    mov eax, cr0
    ret

global cpu_read_cr2
cpu_read_cr2:
    mov eax, cr2
    ret

global cpu_read_cr3
cpu_read_cr3:
    mov eax, cr3
    ret

global cpu_write_cr0
cpu_write_cr0:
    mov eax, [esp+4]
    mov cr0, eax
    ret

global cpu_write_cr3
cpu_write_cr3:
    mov eax, [esp+4]
    mov cr3, eax
    ret

global cpu_invlpg
cpu_invlpg:
    mov eax, [esp+4]
    invlpg [eax]
    ret

global cpu_lgdt
cpu_lgdt:
    mov eax, [esp+4]
    lgdt [eax]
    ret

global cpu_lidt
cpu_lidt:
    mov eax, [esp+4]
    lidt [eax]
    ret
