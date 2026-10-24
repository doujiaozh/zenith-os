/* ZenithOS v6.0 - kernel object heap (bump arena, 8-byte aligned). */
#ifndef ZENITH_HEAP_H
#define ZENITH_HEAP_H

#include <stddef.h>

void  heap_init(void);
void *kmalloc(size_t n);
void *kcalloc(size_t n);
void  kfree(void *p);

#endif /* ZENITH_HEAP_H */
