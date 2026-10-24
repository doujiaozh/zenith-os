; =============================================================================
;  ZenithOS v6.0 - include/config.inc
;  NASM boot contract. Pure constants only: safe to include from 16-bit and
;  32-bit translation units. Must mirror include/config.h.
; =============================================================================
%ifndef ZENITH_CONFIG_INC
%define ZENITH_CONFIG_INC

KERNEL_ORG          equ 0x00010000  ; flat kernel load address
KERNEL_SECTORS      equ 120         ; 60 KiB window, LBA 1..120
KERNEL_MAX_BYTES    equ KERNEL_SECTORS * 512
BOOTINFO_ADDR       equ 0x00000500  ; boot -> kernel hand-off block
BOOTINFO_MAGIC      equ 0x4F4E455A  ; 'ZENO' little-endian
BOOTINFO_VERSION    equ 6
BOOTINFO_ENTRIES    equ BOOTINFO_ADDR + 16
BOOTINFO_MAX_E820   equ 24
KERNEL_STACK_TOP    equ 0x0009F000

BOOT_TYPE_BIOS      equ 0
VIDEO_MODE_13H      equ 0x13

%endif
