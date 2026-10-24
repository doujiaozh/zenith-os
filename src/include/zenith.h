/*
 * ============================================================================
 *  ZenithOS v6.0 - include/zenith.h
 *  Master kernel header: freestanding types, ABI constants and the BootInfo
 *  contract. Every kernel translation unit includes this first.
 * ============================================================================
 */
#ifndef ZENITH_H
#define ZENITH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "bootinfo.h"

/* Architecture word sizes (32-bit kernel). */
typedef uint32_t phys_t;    /* physical address */
typedef uint32_t virt_t;    /* virtual address  */
typedef uint32_t reg_t;     /* machine word     */

#endif /* ZENITH_H */
