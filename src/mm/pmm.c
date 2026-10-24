/* ZenithOS v6.0 - 4 KiB physical page allocator (bitmap, E820 aware). */
#include "mm/pmm.h"
#include "lib/string.h"

static uint32_t *const bitmap = (uint32_t *)(uint32_t)PMM_BITMAP_ADDR;
static uint32_t total = 0;
static uint32_t free_ct = 0;

/* The lowest 1 MiB holds the boot block, kernel, VGA and ISA holes: reserve. */
#define LOW_RESERVED_PAGES (0x100000u >> 12)

static void bit_set(uint32_t p)   { bitmap[p >> 5] |=  (1u << (p & 31)); }
static void bit_clear(uint32_t p) { bitmap[p >> 5] &= ~(1u << (p & 31)); }
static int  bit_test(uint32_t p)  { return (bitmap[p >> 5] >> (p & 31)) & 1u; }

void pmm_init(BootInfo *info) {
    uint64_t max_ram = 0;
    uint32_t i;

    for (i = 0; i < info->e820_count; i++) {
        E820Entry *e = &info->entries[i];
        if (e->type == 1) {
            uint64_t end = e->base + e->length;
            if (end > max_ram) max_ram = end;
        }
    }
    uint64_t capped = max_ram;
    uint64_t limit = (uint64_t)PMM_MAX_PAGES << 12;   /* 512 MiB */
    if (capped > limit) capped = limit;

    total = (uint32_t)(capped >> 12);
    kmemset((void *)bitmap, 0xFF, PMM_BITMAP_BYTES);  /* everything reserved */

    for (i = 0; i < info->e820_count; i++) {
        E820Entry *e = &info->entries[i];
        if (e->type != 1) continue;
        uint32_t first = (uint32_t)(e->base >> 12);
        uint32_t last  = (uint32_t)((e->base + e->length) >> 12);
        if (first < LOW_RESERVED_PAGES) first = LOW_RESERVED_PAGES;
        if (last > total) last = total;
        uint32_t p;
        for (p = first; p < last; p++) bit_clear(p);
    }

    free_ct = 0;
    for (i = LOW_RESERVED_PAGES; i < total; i++)
        if (!bit_test(i)) free_ct++;
}

uint32_t pmm_alloc_page(void) {
    uint32_t p;
    for (p = LOW_RESERVED_PAGES; p < total; p++) {
        if (!bit_test(p)) {
            bit_set(p);
            free_ct--;
            return p << 12;
        }
    }
    return 0;
}

void pmm_free_page(uint32_t addr) {
    uint32_t p = addr >> 12;
    if (p < LOW_RESERVED_PAGES || p >= total) return;
    if (bit_test(p)) { bit_clear(p); free_ct++; }
}

uint32_t pmm_free_pages(void)  { return free_ct; }
uint32_t pmm_total_pages(void) { return total; }
uint32_t pmm_used_pages(void)  { return total - free_ct; }
