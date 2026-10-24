/* ZenithOS v6.0 - identity paging (4 KiB pages), enable MMU. */
#ifndef ZENITH_PAGING_H
#define ZENITH_PAGING_H

#include <stdint.h>
#include "zenith.h"

void paging_identity_init(void);
void paging_enable(void);
void paging_map_page(uint32_t vaddr, uint32_t paddr);

#endif /* ZENITH_PAGING_H */
