/* ZenithOS v6.0 - identity-map the low address space with 4 KiB pages. */
#include "mm/paging.h"
#include "hal/x86/cpu.h"
#include "lib/string.h"
#include "zenith.h"

static uint32_t *const pgdir = (uint32_t *)(uint32_t)PAGE_DIR_ADDR;
static uint32_t *const ptab  = (uint32_t *)(uint32_t)PAGE_TABLES_ADDR;

#define PF_PRESENT 0x1
#define PF_WRITABLE 0x2

void paging_identity_init(void) {
    uint32_t ntables = IDENT_MAP_BYTES >> 22;   /* 4 MiB per table */
    uint32_t i;
    if (ntables < 1) ntables = 1;
    if (ntables > 1024) ntables = 1024;

    kmemset(pgdir, 0, 4096);
    kmemset(ptab, 0, ntables * 4096);

    for (i = 0; i < ntables * 1024; i++)
        ptab[i] = (i << 12) | PF_PRESENT | PF_WRITABLE;
    for (i = 0; i < ntables; i++)
        pgdir[i] = (PAGE_TABLES_ADDR + (i << 12)) | PF_PRESENT | PF_WRITABLE;
}

void paging_enable(void) {
    cpu_write_cr3(PAGE_DIR_ADDR);
    cpu_write_cr0(cpu_read_cr0() | 0x80000000);  /* PG */
}

void paging_map_page(uint32_t vaddr, uint32_t paddr) {
    uint32_t pd = vaddr >> 22;
    uint32_t pt = (vaddr >> 12) & 0x3FF;
    pgdir[pd] = (uint32_t)(uint32_t)ptab + 0;  /* tables already contiguous */
    ptab[(pd << 10) | pt] = (paddr & 0xFFFFF000) | PF_PRESENT | PF_WRITABLE;
}
