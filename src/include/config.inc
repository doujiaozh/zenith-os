; =============================================================================
;  ZenithOS - include/config.inc
;  Single source of truth for the kernel/loader ABI and the physical memory map.
;  Pure constants only: safe to include from 16-bit and 32-bit translation units.
; =============================================================================
%ifndef ZENITH_CONFIG_INC
%define ZENITH_CONFIG_INC

; --- boot contract -----------------------------------------------------------
KERNEL_ORG          equ 0x00010000  ; physical load address of the flat kernel
KERNEL_SECTORS      equ 120         ; 60 KiB window, LBA 1..120 (CHS 2..)
KERNEL_MAX_BYTES    equ KERNEL_SECTORS * 512
BOOTINFO_ADDR       equ 0x00000500  ; boot sector -> kernel hand-off block
BOOTINFO_MAGIC      equ 0x4F4E455A  ; 'ZENO' (little endian)
BOOTINFO_VERSION    equ 1
BOOTINFO_ENTRIES    equ BOOTINFO_ADDR + 16
BOOTINFO_MAX_E820   equ 8

; --- video -------------------------------------------------------------------
VGA_FB              equ 0x000A0000  ; mode 13h linear framebuffer
VGA_WIDTH           equ 320
VGA_HEIGHT          equ 200
VGA_PITCH           equ 320
VGA_BACKBUF         equ 0x00020000  ; 320*200 = 64000 bytes double buffer
VGA_BACKBUF_BYTES   equ VGA_WIDTH * VGA_HEIGHT
VGA_DAC_WRITE       equ 0x3C8
VGA_DAC_DATA        equ 0x3C9
VGA_INPUT_STATUS    equ 0x3DA

; --- physical memory map -----------------------------------------------------
PMM_BITMAP_ADDR     equ 0x00030000  ; 16 KiB -> 131072 pages -> 512 MiB
PMM_BITMAP_BYTES    equ 16384
PMM_MAX_PAGES       equ PMM_BITMAP_BYTES * 8
HEAP_START          equ 0x00034000  ; static kernel arena, ~304 KiB
HEAP_END            equ 0x00070000
AI_SCRATCH          equ 0x00070000
UI_SCRATCH          equ 0x00074000
KERNEL_STACK_TOP    equ 0x0009F000

; --- platform ----------------------------------------------------------------
PIT_HZ              equ 100
PIT_BASE            equ 1193182
KBD_BUF_SIZE        equ 64            ; power of two
SERIAL_PORT         equ 0x3F8
SERIAL_BAUD_DIV     equ 1             ; 115200 baud

; --- AI ----------------------------------------------------------------------
SNN_NEURONS         equ 24
SNN_THRESHOLD       equ 140
SNN_MAX_SPIKES      equ 255

; --- UI ----------------------------------------------------------------------
UI_CX               equ 160
UI_CY               equ 100
UI_NODES            equ 24
UI_EDGES            equ 40

; --- logical keys (kbd_held_mask bit positions) ------------------------------
KEY_W               equ 0
KEY_A               equ 1
KEY_S               equ 2
KEY_D               equ 3
KEY_Q               equ 4
KEY_E               equ 5
KEY_R               equ 6
KEY_SPACE           equ 7
KEY_UP              equ 8
KEY_DOWN            equ 9
KEY_LEFT            equ 10
KEY_RIGHT           equ 11
KEY_ESC             equ 12
KEY_ENTER           equ 13
KEY_C               equ 14

; --- palette roles -----------------------------------------------------------
; Fixed slots 0..15 are stable in every palette so the panic screen stays
; readable; the 16..255 ranges are neon ramps (see src/ui/theme.asm).
COL_BLACK           equ 0
COL_WHITE           equ 1
COL_CYAN            equ 2
COL_MAGENTA         equ 3
COL_RED             equ 4
COL_GREEN           equ 5
COL_AMBER           equ 6
COL_BG              equ 7
COL_DIM             equ 8
COL_BLUE            equ 9
COL_VIOLET          equ 10
COL_HOTPINK         equ 11
COL_RED2            equ 12
COL_TEAL            equ 13
COL_STEEL           equ 14
COL_WHITE2          equ 15
RAMP_NODE           equ 16            ; 16..47  dark blue -> cyan -> white
RAMP_EDGE           equ 48            ; 48..79  deep violet -> magenta
RAMP_GRID           equ 80            ; 80..95  deep teal floor
RAMP_UI             equ 96            ; 96..111 cyan UI ramp
RAMP_LOG            equ 112           ; 112..127 violet log ramp
RAMP_GREEN          equ 128           ; 128..143 OK/status ramp
RAMP_AMBER          equ 144           ; 144..159 warning ramp
RAMP_RED            equ 160           ; 160..175 error ramp
RAMP_BG             equ 176           ; 176..191 near-black background shades

%endif
