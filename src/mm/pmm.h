/* ZenithOS v6.0 - page frame allocator (4 KiB physical pages, bitmap). */
#ifndef ZENITH_PMM_H
#define ZENITH_PMM_H

#include <stdint.h>
#include "zenith.h"

void     pmm_init(BootInfo *info);
uint32_t pmm_alloc_page(void);      /* physical address of a 4 KiB page, 0 if none */
void     pmm_free_page(uint32_t addr);
uint32_t pmm_free_pages(void);
uint32_t pmm_total_pages(void);
uint32_t pmm_used_pages(void);

#endif /* ZENITH_PMM_H */
