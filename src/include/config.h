/*
 * ============================================================================
 *  ZenithOS v6.0 - include/config.h
 *  Single source of truth for the kernel's C-side memory map and ABI.
 *  Must stay in lock-step with include/config.inc (the NASM boot contract).
 * ============================================================================
 */
#ifndef ZENITH_CONFIG_H
#define ZENITH_CONFIG_H

/* --- boot contract (mirrors config.inc) ----------------------------------- */
#define KERNEL_ORG         0x00010000UL  /* flat kernel load address          */
#define KERNEL_SECTORS     120           /* 60 KiB window, LBA 1..120         */
#define KERNEL_MAX_BYTES   (KERNEL_SECTORS * 512UL)

#define BOOTINFO_ADDR      0x00000500UL  /* boot -> kernel hand-off block     */
#define BOOTINFO_MAGIC     0x4F4E455AUL  /* 'ZENO' little-endian               */
#define BOOTINFO_VERSION   6             /* v6.0 kernel                        */
#define BOOTINFO_MAX_E820  24            /* max E820 entries carried to kernel */
#define BOOTINFO_E820_OFF  16            /* entries[] offset inside BootInfo   */

/* boot types */
#define BOOT_TYPE_BIOS     0
#define BOOT_TYPE_UEFI     1

/* video modes reported by the loader */
#define VIDEO_MODE_NONE    0x00
#define VIDEO_MODE_TEXT    0x03
#define VIDEO_MODE_13H     0x13

/* BootInfo.flags bits */
#define BOOTF_VGA          0x01
#define BOOTF_KEYBOARD     0x02
#define BOOTF_HEADLESS     0x04
#define BOOTF_SERIAL       0x08

/* --- video (mode 13h: 320x200x256) --------------------------------------- */
#define VGA_FB             0x000A0000UL  /* linear framebuffer                 */
#define VGA_WIDTH          320
#define VGA_HEIGHT         200
#define VGA_PITCH          320
#define VGA_FB_BYTES       ((uint32_t)VGA_WIDTH * VGA_HEIGHT)
#define VGA_BACKBUF        0x00020000UL  /* double buffer (may swap with FB)  */
#define VGA_DAC_INDEX      0x3C8
#define VGA_DAC_DATA       0x3C9
#define VGA_STATUS         0x3DA
#define VGA_MISC_READ      0x3CC
#define VGA_ADDR_W         0x3D4
#define VGA_ADDR_R         0x3D5

/* --- physical memory map (below the 1 MiB boundary) ------------------------ */
#define PMM_BITMAP_ADDR    0x00030000UL  /* 16 KiB bitmap -> 131072 4K pages  */
#define PMM_BITMAP_BYTES   16384         /* i.e. 512 MiB addressable           */
#define PMM_MAX_PAGES      (PMM_BITMAP_BYTES * 8)

#define HEAP_START         0x00034000UL  /* kernel object arena, ~240 KiB       */
#define HEAP_END           0x00070000UL  /* end-exclusive: last byte is 0x6FFFF */

/* Heap and paging structures must never touch: a 4 KiB guard page sits
 * between HEAP_END and the page directory so a bump-allocator overrun
 * lands in dead RAM before it can corrupt the MMU state. */
#define PAGE_DIR_ADDR      0x00071000UL  /* 4 KiB page directory                */
#define PAGE_TABLES_ADDR   0x00072000UL  /* identity-map page tables            */
#define IDENT_MAP_BYTES    (16u * 1024u * 1024u) /* identity map first 16 MiB  */

#define KERNEL_STACK_TOP   0x0009F000UL

/* --- platform ------------------------------------------------------------- */
#define PIT_HZ             100           /* timer interrupt rate               */
#define PIT_BASE           1193182
#define KBD_BUF_SIZE       64            /* power of two                       */
#define SERIAL_PORT        0x3F8         /* COM1                               */
#define SERIAL_BAUD_DIV    1             /* 115200 baud                        */

/* --- UI -------------------------------------------------------------------- */
#define UI_WIDTH           320
#define UI_HEIGHT          200

/* --- logical keys (kbd state bitmask, see drivers/keyboard.c) ------------- */
#define KEY_W        0
#define KEY_A        1
#define KEY_S        2
#define KEY_D        3
#define KEY_Q        4
#define KEY_E        5
#define KEY_R        6
#define KEY_SPACE    7
#define KEY_UP       8
#define KEY_DOWN     9
#define KEY_LEFT     10
#define KEY_RIGHT    11
#define KEY_ESC      12
#define KEY_ENTER    13
#define KEY_TAB      14
#define KEY_F1       15
#define KEY_F2       16
#define KEY_F3       17

#endif /* ZENITH_CONFIG_H */
