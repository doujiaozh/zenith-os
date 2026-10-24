/* ZenithOS v6.0 - bootstrap heap: monotonic bump allocator over the kernel
 * arena [HEAP_START, HEAP_END). Suitable for kernel objects that live for the
 * whole boot; kfree() is a documented no-op (redo with a free-list allocator
 * before user processes need allocation). */
#include "mm/heap.h"
#include "lib/string.h"
#include "zenith.h"

static uint32_t next = HEAP_START;

void heap_init(void) { next = HEAP_START; }

void *kmalloc(size_t n) {
    uint32_t p = (next + 7) & ~7u;
    if (p + n > (uint32_t)HEAP_END || n == 0) return 0;
    next = p + (uint32_t)n;
    return (void *)p;
}

void *kcalloc(size_t n) {
    void *p = kmalloc(n);
    if (p) kmemset(p, 0, n);
    return p;
}

void kfree(void *p) { (void)p; }
