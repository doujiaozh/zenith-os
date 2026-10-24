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

/* map one 4 KiB page inside the identity window.  The tables sit contiguously
 * after PAGE_TABLES_ADDR, so table pd is ptab[pd << 10]; a directory entry
 * must point at table pd (not at table 0) or the mapping lands in the wrong
 * 4 MiB window.  Returns 0 on success, -1 outside the identity window. */
int paging_map_page(uint32_t vaddr, uint32_t paddr) {
    uint32_t pd = vaddr >> 22;
    uint32_t pt = (vaddr >> 12) & 0x3FF;
    uint32_t ntables = IDENT_MAP_BYTES >> 22;
    if (pd >= ntables) return -1;                     /* no table there */
    pgdir[pd] = (PAGE_TABLES_ADDR + (pd << 12)) | PF_PRESENT | PF_WRITABLE;
    ptab[(pd << 10) | pt] = (paddr & 0xFFFFF000) | PF_PRESENT | PF_WRITABLE;
    cpu_invlpg(vaddr);                                /* drop a stale TLB */
    return 0;
}
