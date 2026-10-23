; =============================================================================
;  ZenithOS - ui/theme.asm
;  Installs the cyberpunk palette.  The 768-byte RGB table is generated at
;  build time (build/generated/palette.inc) from the ramps declared in
;  build.js, so the colour language is reproducible and every consumer
;  references the role constants in include/config.inc rather than raw bytes.
;
;  Fixed slots 0..15 stay stable so the panic screen is readable whether it
;  fires before or after theme_init.
; =============================================================================

[bits 32]

%include "include/config.inc"
%include "palette.inc"

%ifdef ZENITH_OBJ_CHECK
extern vga_set_palette
%endif

; -----------------------------------------------------------------------------
; theme_init: upload all 256 palette entries to the VGA DAC
; -----------------------------------------------------------------------------
theme_init:
    mov esi, theme_palette
    xor edx, edx
    mov ecx, 256
    call vga_set_palette
    ret
