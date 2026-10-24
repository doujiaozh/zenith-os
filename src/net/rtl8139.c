/*
 * ZenithOS v6.0 - net/rtl8139.c - RTL8139C+ driver, register level.
 *
 *  Status: [已实现] integrated copy of the netdev/ sandbox driver, wired to
 *  the PCI layer (I/O base + IRQ line) and PMM (DMA regions).
 *
 * Two deliberate disciplines run through this file:
 *
 *  1. Every single register touch is an outb/inb/... call from the HAL. The
 *     HAL is the only hardware entry into ZenithOS (docs/ARCHITECTURE.md 3),
 *     and polluting a NIC driver with raw port instructions would set the
 *     wrong precedent for every driver that follows.
 *  2. Every write to a register that the chip may post (PCI write buffers)
 *     is followed by a read-back of the same register. This "flush" is the
 *     trick the reference driver uses to survive the 8139's documented
 *     posted-write bugginess; it costs one I/O cycle and is marked with a
 *     comment so reviewers see why the read result is discarded.
 *
 * Bring-up sequence mirrors 8139too's rtl8139_hw_start(); the data path
 * mirrors its Rx/Tx loops. Where ZenithOS differs (NetBuf instead of sk_buff,
 * PIO instead of MMIO, no NAPI) the change is called out in a comment.
 */
#include "zenith.h"
#include "rtl8139.h"
#include "hal/x86/port.h"
#include "lib/string.h"

/* --- small typed helpers over the HAL (base + offset) --------------------- */

static void rt_w8(uint16_t io, uint16_t reg, uint8_t value)
{
    outb(io + reg, value);
}

static uint8_t rt_r8(uint16_t io, uint16_t reg)
{
    return inb(io + reg);
}

static void rt_w16(uint16_t io, uint16_t reg, uint16_t value)
{
    outw(io + reg, value);
}

static uint16_t rt_r16(uint16_t io, uint16_t reg)
{
    return inw(io + reg);
}

static void rt_w32(uint16_t io, uint16_t reg, uint32_t value)
{
    outl(io + reg, value);
}

static uint32_t rt_r32(uint16_t io, uint16_t reg)
{
    return inl(io + reg);
}

/* write + read back: flush the chip's posted write buffers (8139 erratum) */
static void rt_w8f(uint16_t io, uint16_t reg, uint8_t value)
{
    outb(io + reg, value);
    (void)inb(io + reg);
}

static void rt_w32f(uint16_t io, uint16_t reg, uint32_t value)
{
    outl(io + reg, value);
    (void)inl(io + reg);
}

/* one eeprom clock period: the reference delays by re-reading Cfg9346 */
static void rt_eeprom_delay(uint16_t io)
{
    (void)inb(io + RT_CFG9346);
}

/* --- hardware access ------------------------------------------------------ */

int rtl8139_probe(uint16_t io)
{
    if (io == 0)
        return -1;                   /* never probe grandfather ports */
    if (rt_r32(io, RT_TXCONFIG) == 0xFFFFFFFFu)
        return -1;                   /* dead window: no chip behind it */
    return 0;
}

int rtl8139_reset(uint16_t io)
{
    int i;

    if (io == 0)
        return -1;
    rt_w8(io, RT_HLTCLK, 'R');       /* run the clock: leave low-power mode */
    rt_w8(io, RT_CHIPCMD, RT_CMD_RST);
    for (i = 0; i < 1000; i++) {
        io_wait();                   /* ~1 us granularity from the HAL */
        if ((rt_r8(io, RT_CHIPCMD) & RT_CMD_RST) == 0)
            return 0;
    }
    return -1;                       /* reset never completed: chip is gone */
}

int rtl8139_eeprom_read(uint16_t io, int location, int addr_len)
{
    int read_cmd = location | (RT_EE_READ_CMD << addr_len);
    int i;
    uint32_t retval = 0;

    if (io == 0)
        return -1;
    rt_w8(io, RT_CFG9346, RT_EE_ENB & (uint8_t)~RT_EE_CS);
    rt_w8(io, RT_CFG9346, RT_EE_ENB);
    rt_eeprom_delay(io);
    for (i = 4 + addr_len; i >= 0; i--) {
        uint8_t dataval = (read_cmd & (1 << i)) ? RT_EE_DATA_WRITE : 0;
        rt_w8(io, RT_CFG9346, RT_EE_ENB | dataval);
        rt_eeprom_delay(io);
        rt_w8(io, RT_CFG9346, RT_EE_ENB | dataval | RT_EE_SHIFT_CLK);
        rt_eeprom_delay(io);
    }
    rt_w8(io, RT_CFG9346, RT_EE_ENB);
    rt_eeprom_delay(io);
    for (i = 16; i > 0; i--) {
        rt_w8(io, RT_CFG9346, RT_EE_ENB | RT_EE_SHIFT_CLK);
        rt_eeprom_delay(io);
        retval = (retval << 1) |
                 ((rt_r8(io, RT_CFG9346) & RT_EE_DATA_READ) ? 1u : 0u);
        rt_w8(io, RT_CFG9346, RT_EE_ENB);
        rt_eeprom_delay(io);
    }
    rt_w8(io, RT_CFG9346, 0);        /* terminate the EEPROM access */
    rt_eeprom_delay(io);
    return (int)retval;
}

int rtl8139_read_mac(uint16_t io, uint8_t out[6])
{
    int i;

    if (io == 0 || out == NULL)
        return -1;
    for (i = 0; i < 6; i++)
        out[i] = rt_r8(io, RT_MAC0 + (uint16_t)i);
    return 0;
}

/* --- bring-up / teardown --------------------------------------------------- */

int rtl8139_init(Rtl8139 *d, uint16_t io, uint8_t *rx_ring, uint8_t *tx_bufs)
{
    int i;

    if (d == NULL || io == 0 || rx_ring == NULL || tx_bufs == NULL)
        return -1;
    kmemset(d, 0, sizeof(*d));
    d->io = io;
    d->rx_ring = rx_ring;
    d->tx_bufs = tx_bufs;
    d->tx_flag = ((uint32_t)RT_TX_FIFO_THRESH << 11) & RT_TSD_ERTXTH_MASK;

    if (rtl8139_reset(io) != 0)
        return -1;
    if (rtl8139_read_mac(io, d->mac) != 0)
        return -1;

    /* --- rtl8139_hw_start, in the reference order ------------------------ */
    rt_w8(io, RT_HLTCLK, 'R');

    rt_w8f(io, RT_CFG9346, RT_CFG9346_UNLOCK);
    for (i = 0; i < 6; i++)          /* write the MAC back: we own it now */
        rt_w8f(io, RT_MAC0 + (uint16_t)i, d->mac[i]);

    rt_w32f(io, RT_RXBUF, (uint32_t)(uintptr_t)rx_ring);

    /* must enable Tx/Rx before programming the thresholds */
    rt_w8(io, RT_CHIPCMD, RT_CMD_RXENB | RT_CMD_TXENB);

    rt_w32(io, RT_RXCONFIG,
           d->rx_config =
               RT_RXCFG_RECV_32K | RT_RXCFG_NOWRAP |
               ((uint32_t)RT_RX_FIFO_THRESH << RT_RXCFG_FIFO_SHIFT) |
               ((uint32_t)RT_RX_DMA_BURST << RT_RXCFG_DMA_SHIFT) |
               RT_RXCFG_ACCEPT_BCAST | RT_RXCFG_ACCEPT_MYPHYS);
    rt_w32(io, RT_TXCONFIG,
           RT_TXCFG_IFG96 |
           ((uint32_t)RT_TX_DMA_BURST << RT_TXCFG_DMA_SHIFT) |
           ((uint32_t)RT_TX_RETRY << RT_TXCFG_RETRY_SHIFT));

    rt_w8(io, RT_CFG9346, RT_CFG9346_LOCK);  /* lock Config0-4 + BMCR */

    for (i = 0; i < RT_NUM_TX_DESC; i++) {
        uint8_t *buf = tx_bufs + (size_t)i * RT_TX_BOUNCE_SIZE;
        rt_w32f(io, RT_TSAD0 + (uint16_t)i * 4u, (uint32_t)(uintptr_t)buf);
    }

    rt_w32(io, RT_RXMISSED, 0);

    {
        uint16_t multi = rt_r16(io, RT_MULTIINTR);
        rt_w16(io, RT_MULTIINTR, multi & 0xF000u);   /* no early-Rx IRQs */
    }

    {
        uint8_t cmd = rt_r8(io, RT_CHIPCMD);
        if ((cmd & (RT_CMD_RXENB | RT_CMD_TXENB)) !=
            (RT_CMD_RXENB | RT_CMD_TXENB))
            rt_w8(io, RT_CHIPCMD, RT_CMD_RXENB | RT_CMD_TXENB);
    }

    rt_w16(io, RT_INTRMASK, RT_ISR_ALL);
    return 0;
}

void rtl8139_stop(Rtl8139 *d)
{
    if (d == NULL || d->io == 0)
        return;
    rt_w16(d->io, RT_INTRMASK, 0);
    rt_w8(d->io, RT_CHIPCMD, 0);     /* stop the Tx and Rx DMA engines */
    d->rx_ring = NULL;
}

int rtl8139_link_ok(const Rtl8139 *d)
{
    if (d == NULL || d->io == 0)
        return 0;
    return (rt_r16(d->io, RT_CSCR) & RT_CSCR_LINK_OK) != 0;
}

/* --- interrupt interface --------------------------------------------------- */

uint16_t rtl8139_int_status(const Rtl8139 *d)
{
    if (d == NULL || d->io == 0)
        return 0;
    return rt_r16(d->io, RT_INTRSTATUS) & RT_ISR_ALL;
}

void rtl8139_int_ack(Rtl8139 *d, uint16_t bits)
{
    if (d == NULL || d->io == 0)
        return;
    rt_w16(d->io, RT_INTRSTATUS, bits);
    (void)rt_r16(d->io, RT_INTRSTATUS);   /* flush: ISR is RW1C */
}

/* --- Rx -------------------------------------------------------------------- *
 * The chip DMA's a 4-byte header (status, size-including-FCS) in front of
 * every frame and bumps its own CBR; the host tracks cur_rx and publishes
 * progress through CAPR. With RxNoWrap set, frames never straddle the ring
 * end (they may run up to RT_RX_RING_WRAP_PAD bytes past the nominal end,
 * which is why the caller reserves RT_RX_RING_TOTAL), so a single
 * netbuf_append covers each frame.
 */

/* Re-arm the receiver after an error, as the reference's rtl8139_rx_err
 * does (its newer path, not the old full reset): toggle RxEnb off and back
 * on, restore the RCR, rewind cur_rx. Known simplification: the reference
 * does not rewrite CAPR here and neither do we; on real hardware bring-up
 * this is the first thing to scrutinize. */
static void rtl8139_stop_rx(Rtl8139 *d)
{
    uint8_t cmd = rt_r8(d->io, RT_CHIPCMD);

    rt_w8(d->io, RT_CHIPCMD, (uint8_t)(cmd & ~RT_CMD_RXENB));
    rt_w8(d->io, RT_CHIPCMD, cmd);
    rt_w32(d->io, RT_RXCONFIG, d->rx_config);
    d->cur_rx = 0;
    d->rx_overflow++;
}

size_t rtl8139_rx(Rtl8139 *d, NetBuf **out, size_t max)
{
    size_t got = 0;

    if (d == NULL || d->rx_ring == NULL || out == NULL || max == 0)
        return 0;
    while (got < max) {
        if (rt_r8(d->io, RT_CHIPCMD) & RT_CMD_BUFE)
            break;                   /* ring drained */
        {
            uint32_t off = d->cur_rx % RT_RX_RING_BYTES;
            uint32_t hdr;            /* ring memory may be unaligned: memcpy */
            uint16_t status;
            uint16_t size;
            uint16_t pkt;
            NetBuf *nb;

            kmemcpy(&hdr, d->rx_ring + off, RT_RX_HDR_BYTES);
            status = (uint16_t)(hdr & 0xFFFFu);
            size = (uint16_t)(hdr >> 16);
            if (size == RT_RX_FIFO_COPY)
                break;               /* frame still streaming in from the FIFO */
            if (size < RT_RX_MIN_FRAME || size > RT_MAX_TX_FRAME + RT_RX_FCS_BYTES ||
                (status & RT_RXS_OK) == 0) {
                d->rx_err++;
                rtl8139_stop_rx(d);  /* lost in the ring: re-arm the receiver */
                break;
            }
            pkt = (uint16_t)(size - RT_RX_FCS_BYTES);
            nb = netbuf_alloc(0, pkt);
            if (nb == NULL) {
                d->rx_dropped++;     /* NetBuf pool empty: drop, keep ring */
            } else if (netbuf_append(nb, d->rx_ring + off + RT_RX_HDR_BYTES,
                                     pkt) != 0) {
                d->rx_dropped++;
                netbuf_free(nb);
            } else {
                nb->tag = status;    /* caller-visible Rx status snapshot */
                out[got++] = nb;
                d->rx_ok++;
            }
            /* consume: 4-byte header + size, 4-byte aligned */
            d->cur_rx = (d->cur_rx + (uint32_t)size + RT_RX_HDR_BYTES + 3u) & ~3u;
            rt_w16(d->io, RT_RXBUFPTR,
                   (uint16_t)(d->cur_rx - RT_CAPR_LAG));
        }
    }
    return got;
}

/* --- Tx -------------------------------------------------------------------- *
 * Four bounce buffers, four descriptors. Writing TSD arms the transfer;
 * the chip reports completion by setting TOK/TUN/ABT in the same register.
 * cur_tx/dirty_tx are free-running 16-bit counters, so the ring-full test is
 * a plain subtraction.
 */
size_t rtl8139_tx_reclaim(Rtl8139 *d)
{
    size_t freed = 0;

    if (d == NULL || d->io == 0)
        return 0;
    while (d->dirty_tx != d->cur_tx) {
        uint8_t slot = (uint8_t)(d->dirty_tx % RT_NUM_TX_DESC);
        uint32_t tsd = rt_r32(d->io, RT_TSD0 + (uint16_t)slot * 4u);

        if ((tsd & (RT_TSD_TOK | RT_TSD_TUN | RT_TSD_ABT)) == 0)
            break;                   /* chip still owns this one */
        if (tsd & RT_TSD_TOK)
            d->tx_ok++;
        else
            d->tx_err++;
        d->dirty_tx++;
        freed++;
    }
    return freed;
}

int rtl8139_tx(Rtl8139 *d, const void *data, size_t len)
{
    uint8_t slot;
    uint8_t *buf;
    size_t n;

    if (d == NULL || data == NULL || len == 0 || len > RT_MAX_TX_FRAME)
        return -1;
    if ((uint16_t)(d->cur_tx - d->dirty_tx) >= RT_NUM_TX_DESC) {
        d->tx_ring_full++;
        return -1;                   /* reclaim first: driver keeps ownership */
    }
    slot = (uint8_t)(d->cur_tx % RT_NUM_TX_DESC);
    buf = d->tx_bufs + (size_t)slot * RT_TX_BOUNCE_SIZE;
    n = len;
    if (n < RT_MIN_TX_FRAME)
        n = RT_MIN_TX_FRAME;
    kmemcpy(buf, data, len);
    if (len < RT_MIN_TX_FRAME)
        kmemset(buf + len, 0, RT_MIN_TX_FRAME - len);   /* no auto-pad in HW */
    rt_w32(d->io, RT_TSD0 + (uint16_t)slot * 4u,
           d->tx_flag | (uint32_t)n);
    d->cur_tx++;
    return 0;
}
