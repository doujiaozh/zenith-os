/* ZenithOS v6.0 - net/netbuf.c - static-pool packet buffers, sk_buff style.
 * Integrated copy of the netdev/ sandbox module, in service of rtl8139_rx. */
#include "zenith.h"
#include "netbuf.h"
#include "lib/string.h"

static NetBuf g_netbuf_pool[NETBUF_POOL_SIZE];
static NetBuf *g_netbuf_free_head;   /* intrusive free-list, O(1) push/pop  */

void netbuf_init(void)
{
    unsigned i;

    kmemset(g_netbuf_pool, 0, sizeof(g_netbuf_pool));
    g_netbuf_free_head = NULL;
    for (i = 0; i < NETBUF_POOL_SIZE; i++) {
        g_netbuf_pool[i].next = g_netbuf_free_head;
        g_netbuf_free_head = &g_netbuf_pool[i];
    }
}

NetBuf *netbuf_alloc(size_t headroom, size_t capacity)
{
    NetBuf *nb;

    if (headroom + capacity > NETBUF_MAX_PAYLOAD)
        return NULL;
    nb = g_netbuf_free_head;
    if (nb == NULL)
        return NULL;                 /* pool exhausted: back off, do not panic */
    g_netbuf_free_head = nb->next;
    nb->head = (uint16_t)headroom;
    nb->len = 0;
    nb->refs = 1;
    nb->tag = 0;
    nb->next = NULL;
    return nb;
}

NetBuf *netbuf_ref(NetBuf *nb)
{
    if (nb == NULL || nb->refs == 0)
        return NULL;                 /* not an owned buffer */
    if (nb->refs == 0xFFFF)
        return NULL;                 /* saturated: never wrap to 0 in use */
    nb->refs++;
    return nb;
}

void netbuf_free(NetBuf *nb)
{
    if (nb == NULL || nb->refs == 0)
        return;                      /* NULL or already free: defensive no-op */
    nb->refs--;
    if (nb->refs != 0)
        return;
    /* last owner: scrub stale state and loop the buffer back into the pool */
    nb->head = 0;
    nb->len = 0;
    nb->tag = 0;
    nb->next = g_netbuf_free_head;
    g_netbuf_free_head = nb;
}

int netbuf_reserve(NetBuf *nb, size_t n)
{
    if (nb == NULL || nb->len != 0)
        return -1;                   /* reserving a non-empty buffer */
    if (n > (size_t)(NETBUF_MAX_PAYLOAD - nb->head))
        return -1;
    nb->head += (uint16_t)n;
    return 0;
}

uint8_t *netbuf_push(NetBuf *nb, size_t n)
{
    if (nb == NULL || n > nb->head)
        return NULL;                 /* insufficient headroom */
    nb->head -= (uint16_t)n;
    nb->len += (uint16_t)n;
    return nb->storage + nb->head;
}

int netbuf_pull(NetBuf *nb, size_t n)
{
    if (nb == NULL || n > nb->len)
        return -1;
    nb->head += (uint16_t)n;
    nb->len -= (uint16_t)n;
    return 0;
}

int netbuf_trim(NetBuf *nb, size_t n)
{
    if (nb == NULL || n > nb->len)
        return -1;
    nb->len -= (uint16_t)n;
    return 0;
}

int netbuf_append(NetBuf *nb, const void *data, size_t n)
{
    if (nb == NULL || data == NULL)
        return -1;
    if (n > (size_t)(NETBUF_MAX_PAYLOAD - nb->head) - nb->len)
        return -1;                   /* would overflow the backing store */
    kmemcpy(nb->storage + nb->head + nb->len, data, n);
    nb->len += (uint16_t)n;
    return 0;
}

uint8_t *netbuf_data(const NetBuf *nb)
{
    if (nb == NULL)
        return NULL;
    return nb->storage + nb->head;
}

size_t netbuf_len(const NetBuf *nb)
{
    if (nb == NULL)
        return 0;
    return nb->len;
}

size_t netbuf_headroom(const NetBuf *nb)
{
    if (nb == NULL)
        return 0;
    return nb->head;
}

size_t netbuf_tailroom(const NetBuf *nb)
{
    if (nb == NULL)
        return 0;
    return (size_t)(NETBUF_MAX_PAYLOAD - nb->head) - nb->len;
}

size_t netbuf_pool_size(void)
{
    return NETBUF_POOL_SIZE;
}

size_t netbuf_pool_free(void)
{
    const NetBuf *nb;
    size_t n = 0;

    for (nb = g_netbuf_free_head; nb != NULL; nb = nb->next)
        n++;
    return n;
}
