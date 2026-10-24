/* ZenithOS v6.0 - memory manager bring-up. */
#include "mm/mm.h"
#include "mm/pmm.h"
#include "mm/paging.h"
#include "mm/heap.h"

void mm_init(BootInfo *info) {
    pmm_init(info);          /* 4 KiB page-frame bitmap from the E820 map */
    paging_identity_init();  /* build 4 KiB identity tables                */
    paging_enable();         /* flip CR0.PG (+CR3), turn the MMU on        */
    heap_init();             /* kernel object arena                        */
}
