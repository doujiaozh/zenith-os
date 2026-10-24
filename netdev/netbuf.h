/*
 * ============================================================================
 *  ZenithOS v6.0 - netdev/netbuf.h - zero-copy packet buffer pool
 *
 *  Status: [已编译·未接线] skeleton in the netdev/ staging directory. The
 *  module compiles standalone (tcc -nostdinc, see docs/NETWORK.md) but is not
 *  wired into src/net/ yet; moving it is the Lead's call.
 *
 *  A NetBuf is a flat 1536-byte backing store plus (head, len) cursors, so a
 *  protocol stack can push/pull headers in place instead of copying: the same
 *  buffer carries an Ethernet frame, then is pulled to the IPv4 header, then
 *  to the TCP segment, with zero memmove. refs gives sharing between the Rx
 *  path, retransmit queues and the debug sink; the last owner returns the
 *  buffer to the pool.
 *
 *  Allocation is a fixed static pool with an intrusive free-list, not the
 *  kernel heap: the heap is [已编译·未接线] and a network stack that can
 *  exhaust/fragment the kernel arena is worse than one with a bounded budget.
 *  16 * 1536 = 24 KiB of .bss once linked; current kernel layout leaves
 *  ~39 KiB between bss_end (0x166E8) and the VGA back buffer (0x20000) - tune
 *  NETBUF_POOL_SIZE before final integration.
 * ============================================================================
 */
#ifndef ZENITH_NETBUF_H
#define ZENITH_NETBUF_H

#include "zenith.h"

/* --- pool geometry -------------------------------------------------------- */

#define NETBUF_MAX_PAYLOAD   1536   /* full Ethernet frame incl. 4 B FCS slack */
#define NETBUF_POOL_SIZE     16     /* powers of two keep the modulo cheap     */
#define NETBUF_POOL_BYTES    (NETBUF_POOL_SIZE * NETBUF_MAX_PAYLOAD)

/*
 * Flat buffer with sk_buff-style cursors:
 *
 *   storage[1536]
 *   |<--- headroom --->|<--- len --->|<--- tailroom --->|
 *   ^                  ^             ^                  ^
 *   storage            data          data+len          end
 *
 * Prepending a header moves `head` down (netbuf_push), stripping one moves it
 * up (netbuf_pull) - neither copies a byte. The intrusive `next` pointer is
 * the pool free-list link while refs == 0, and is free for the caller to use
 * as a queue link (Tx ring, retransmit queue) while the buffer is owned.
 */
typedef struct NetBuf {
    uint8_t          storage[NETBUF_MAX_PAYLOAD];
    uint16_t         head;    /* offset of first data byte into storage      */
    uint16_t         len;     /* valid data bytes                           */
    uint16_t         refs;    /* reference count; 0 == free in pool         */
    uint16_t         tag;     /* caller scratch (EtherType, ring slot, ...)  */
    struct NetBuf   *next;    /* free-list / queue link (see above)          */
} NetBuf;

/* --- lifecycle ------------------------------------------------------------ */

void     netbuf_init(void);   /* reset the pool: all buffers free-linked     */

/*
 * Take a buffer out of the pool with `headroom` free bytes in front of the
 * data start and room for `capacity` payload bytes behind it
 * (headroom + capacity <= NETBUF_MAX_PAYLOAD). The buffer starts empty:
 * len == 0, refs == 1, tag == 0. Returns NULL when the pool is exhausted or
 * the request does not fit - callers must back off, never panic.
 */
NetBuf  *netbuf_alloc(size_t headroom, size_t capacity);

/* Share: +1 ref, same pointer. Saturates at 0xFFFF instead of wrapping. */
NetBuf  *netbuf_ref(NetBuf *nb);

/* Release: -1 ref; the last owner pushes the buffer back to the pool.
 * No-op on NULL or an already-free buffer (defensive: a network input path
 * must never crash the kernel on a double free). */
void     netbuf_free(NetBuf *nb);

/* --- cursors (zero-copy header add/remove) -------------------------------- */

/* On an empty buffer only: move the data start `n` bytes forward, growing
 * headroom (sk_buff skb_reserve). Data buffers must be empty. */
int      netbuf_reserve(NetBuf *nb, size_t n);

/* Prepend `n` bytes of header space: head -= n, len += n. Returns the new
 * data pointer, NULL when headroom is insufficient. */
uint8_t *netbuf_push(NetBuf *nb, size_t n);

/* Strip `n` bytes of header: head += n, len -= n. */
int      netbuf_pull(NetBuf *nb, size_t n);

/* Drop `n` bytes from the tail (e.g. the Ethernet FCS). */
int      netbuf_trim(NetBuf *nb, size_t n);

/* Copy `n` bytes onto the tail; len += n. This is the one copying operation,
 * used where data crosses an address-space boundary (DMA ring -> NetBuf). */
int      netbuf_append(NetBuf *nb, const void *data, size_t n);

/* --- accessors ------------------------------------------------------------ */

uint8_t *netbuf_data(const NetBuf *nb);
size_t   netbuf_len(const NetBuf *nb);
size_t   netbuf_headroom(const NetBuf *nb);
size_t   netbuf_tailroom(const NetBuf *nb);
size_t   netbuf_pool_size(void);
size_t   netbuf_pool_free(void);   /* free slots right now (diagnostics)     */

#endif /* ZENITH_NETBUF_H */
