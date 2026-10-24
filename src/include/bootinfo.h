/*
 * ============================================================================
 *  ZenithOS v6.0 - include/bootinfo.h
 *  The loader -> kernel hand-off structure. Written by boot/bios (16-bit) or
 *  boot/uefi (64-bit) into BOOTINFO_ADDR, read by kernel_main (32-bit C).
 *  Layout is part of the stable ABI and must match config.inc exactly.
 * ============================================================================
 */
#ifndef ZENITH_BOOTINFO_H
#define ZENITH_BOOTINFO_H

#include <stdint.h>
#include "config.h"

/* One E820 physical-memory-range descriptor (24 bytes, ACPI 3.0 shape). */
typedef struct {
    uint64_t base;      /* region start address                  */
    uint64_t length;    /* region length in bytes                */
    uint32_t type;      /* 1 = RAM, 2 = reserved, 3/4 = ACPI ... */
    uint32_t attrs;     /* ACPI 3.0 extended attributes          */
} __attribute__((packed)) E820Entry;

/* 16-byte fixed header followed by the E820 descriptor table. */
typedef struct {
    uint32_t magic;         /* BOOTINFO_MAGIC ('ZENO')            */
    uint16_t version;       /* BOOTINFO_VERSION (6)               */
    uint16_t e820_count;    /* number of valid entries[]          */
    uint8_t  boot_drive;    /* BIOS drive (0x00 floppy / 0x80 hd) */
    uint8_t  boot_type;     /* BOOT_TYPE_BIOS / BOOT_TYPE_UEFI    */
    uint8_t  video_mode;    /* VIDEO_MODE_13H / _TEXT / _NONE     */
    uint8_t  flags;         /* BOOTF_* bits                       */
    uint16_t checksum;      /* additive checksum of header (0 = off) */
    uint16_t reserved;
    E820Entry entries[BOOTINFO_MAX_E820];
} __attribute__((packed)) BootInfo;

#endif /* ZENITH_BOOTINFO_H */
