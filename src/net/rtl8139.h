/*
 * ============================================================================
 *  ZenithOS v6.0 - net/rtl8139.h - RTL8139C+ Fast Ethernet driver
 *
 *  Status: [已实现] integrated copy of the netdev/ sandbox driver (reviewed
 *  register-by-register against Linux drivers/net/ethernet/realtek/8139too.c).
 *  The I/O base and DMA regions come from the PCI layer + PMM; the IRQ line
 *  is registered through arch/x86_32/isr.c. Runtime-verified in QEMU user
 *  networking (ARP request -> gateway reply), see docs/NETWORK.md.
 *
 *  What is here:
 *    - the full register map of the RTL8139C+ I/O window (0x00-0xD8), with
 *      the bit definitions the driver actually touches (ChipCmd, ISR/IMR,
 *      TSD, RxStatus, RCR, TCR, Cfg9346/93C46 EEPROM, CSCR link);
 *    - soft reset, probe (TxConfig read-back) and 93C46 EEPROM bit-bang;
 *    - the hw_start bring-up sequence exactly as the reference does it;
 *    - Rx ring polling into NetBufs and a 4-descriptor Tx bounce ring.
 *
 *  What is intentionally NOT here:
 *    - PCI config-space access: the I/O base `io` is supplied by the PCI
 *      layer (src/drivers/pci.c); rtl8139_init() refuses io == 0 rather than
 *      probing grandfather ports.
 *    - interrupt handler wiring: rtl8139_int_status/ack are provided for the
 *      IRQ layer to call; the driver itself never installs gates.
 *    - bus-master DMA safety policy: the caller hands in DMA-capable regions
 *      (rx_ring / tx_bufs); see the RTL_RX_RING_* constants for the sizes
 *      the hardware requires.
 * ============================================================================
 */
#ifndef ZENITH_RTL8139_H
#define ZENITH_RTL8139_H

#include "zenith.h"
#include "netbuf.h"

/* --- PCI identity (consumed by the future PCI config-space layer) ---------- */

#define RTL8139_PCI_VENDOR_ID   0x10EC   /* Realtek Semiconductor Corp. */
#define RTL8139_PCI_DEVICE_ID   0x8139   /* RTL-8139/8139C/8139D (+clones) */
#define RTL8139_IO_WINDOW_BYTES 0x80     /* minimum usable I/O window span */

/* --- register offsets from the I/O base ----------------------------------- *
 * RTL8139C+ datasheet register map, cross-checked against 8139too.c.
 */
#define RT_MAC0         0x00    /* IDR0..5  Ethernet address, R/W            */
#define RT_MAR0         0x08    /* MAR0..7  64-entry multicast hash, R/W     */
#define RT_TSD0         0x10    /* TSD0..3  Tx status/descriptor, 4 x 32-bit */
#define RT_TSAD0        0x20    /* TSAD0..3 Tx buffer base address, 4x32-bit */
#define RT_RXBUF        0x30    /* RBSTART  Rx ring base address, 32-bit     */
#define RT_CHIPCMD      0x37    /* CR       command register, 8-bit          */
#define RT_RXBUFPTR     0x38    /* CAPR     host Rx read offset, 16-bit, WO */
#define RT_RXBUFADDR    0x3A    /* CBR      chip Rx write offset, 16-bit, RO */
#define RT_INTRMASK     0x3C    /* IMR      interrupt mask, 16-bit           */
#define RT_INTRSTATUS   0x3E    /* ISR      interrupt status, 16-bit, RW1C   */
#define RT_TXCONFIG     0x40    /* TCR      Tx configuration, 32-bit         */
#define RT_RXCONFIG     0x44    /* RCR      Rx configuration, 32-bit         */
#define RT_TIMER        0x48    /* general-purpose counter, 32-bit           */
#define RT_RXMISSED     0x4C    /* missed packet counter, 24-bit, W1C        */
#define RT_CFG9346      0x50    /* 93C46 EEPROM command + config lock, 8-bit */
#define RT_CONFIG0      0x51
#define RT_CONFIG1      0x52
#define RT_TIMERINT     0x54
#define RT_MEDIASTATUS  0x58
#define RT_CONFIG3      0x59
#define RT_CONFIG4      0x5A    /* absent on RTL-8139A                       */
#define RT_HLTCLK       0x5B    /* 'R' run / 'H' halt the clock (old chips)  */
#define RT_MULTIINTR    0x5C
#define RT_TXSUMMARY    0x60
#define RT_BMCR         0x62    /* internal PHY basic mode control            */
#define RT_BMSR         0x64
#define RT_NWAYADVERT   0x66
#define RT_NWAYLPAR     0x68
#define RT_NWAYEXP      0x6A
#define RT_CSCR         0x74    /* chip status: link bit used here           */
#define RT_CONFIG5      0xD8    /* absent on RTL-8139A                       */

/* --- ChipCmd (RT_CHIPCMD) bits -------------------------------------------- */

#define RT_CMD_RST      0x10    /* soft reset, self-clearing when done       */
#define RT_CMD_RXENB    0x08    /* Rx enable                                 */
#define RT_CMD_TXENB    0x04    /* Tx enable                                 */
#define RT_CMD_BUFE     0x01    /* RxBufEmpty: set while the ring is empty   */

/* --- ISR / IMR bits (8139too naming) -------------------------------------- */

#define RT_ISR_ROK      0x0001  /* receive OK                                */
#define RT_ISR_RER      0x0002  /* receive error                                */
#define RT_ISR_TOK      0x0004  /* transmit OK                               */
#define RT_ISR_TER      0x0008  /* transmit error                            */
#define RT_ISR_RXOVW    0x0010  /* Rx ring overflow                          */
#define RT_ISR_RXUND    0x0020  /* Rx underrun (also a link-change carrier)  */
#define RT_ISR_FIFOOVW  0x0040  /* Rx FIFO overflow                          */
#define RT_ISR_PCSTIMEOUT 0x4000
#define RT_ISR_PCIERR   0x8000
#define RT_ISR_RX_ACK   (RT_ISR_FIFOOVW | RT_ISR_RXOVW | RT_ISR_ROK)
#define RT_ISR_ALL      (RT_ISR_PCIERR | RT_ISR_PCSTIMEOUT | RT_ISR_RXUND | \
                         RT_ISR_RXOVW | RT_ISR_FIFOOVW | RT_ISR_TER | \
                         RT_ISR_TOK | RT_ISR_RER | RT_ISR_ROK)

/* --- TSD (Tx status/descriptor) bits -------------------------------------- *
 * The driver writes SIZE (+ERTXTH) to arm a descriptor; the chip writes back
 * TOK/TUN/ABT/CRS when the transfer ends. SIZE must not exceed 1792 bytes.
 */
#define RT_TSD_SIZE_MASK    0x00001FFFu  /* frame size, bits 0-12           */
#define RT_TSD_HOSTOWNS     0x00002000u  /* host may reclaim (Linux: TxHostOwns) */
#define RT_TSD_TUN         0x00004000u  /* Tx FIFO underrun               */
#define RT_TSD_TOK         0x00008000u  /* transmit OK (device writes)     */
#define RT_TSD_ERTXTH_MASK 0x003F0000u  /* early Tx threshold, bits 16-21  */
#define RT_TSD_NCC_SHIFT   24           /* collision count, bits 24-27     */
#define RT_TSD_NCC_MASK    0x0F000000u
#define RT_TSD_OWIN        0x20000000u  /* out-of-window collision         */
#define RT_TSD_ABT         0x40000000u  /* transmit aborted                */
#define RT_TSD_CRS         0x80000000u  /* carrier lost (always set@100Mb) */

/* --- Rx status bits (first 16 bits of each ring frame header) ------------- */

#define RT_RXS_MULTICAST 0x8000
#define RT_RXS_PHYSICAL  0x4000
#define RT_RXS_BROADCAST 0x2000
#define RT_RXS_BADSYMBOL 0x0020
#define RT_RXS_RUNT      0x0010
#define RT_RXS_TOOLONG   0x0008
#define RT_RXS_CRCERR    0x0004
#define RT_RXS_BADALIGN  0x0002
#define RT_RXS_OK        0x0001

/* --- RCR (RxConfig) bits -------------------------------------------------- */

#define RT_RXCFG_ACCEPT_ERR      0x20
#define RT_RXCFG_ACCEPT_RUNT     0x10
#define RT_RXCFG_ACCEPT_BCAST    0x08
#define RT_RXCFG_ACCEPT_MCAST    0x04
#define RT_RXCFG_ACCEPT_MYPHYS   0x02
#define RT_RXCFG_ACCEPT_ALLPHYS  0x01
#define RT_RXCFG_FIFO_SHIFT      13
#define RT_RXCFG_DMA_SHIFT       8
#define RT_RXCFG_RECV_8K         0x0000
#define RT_RXCFG_RECV_16K        0x0800
#define RT_RXCFG_RECV_32K        0x1000
#define RT_RXCFG_RECV_64K        0x1800
#define RT_RXCFG_NOWRAP          0x0080  /* disable packet wrap at ring end */

/* --- TCR (TxConfig) bits -------------------------------------------------- */

#define RT_TXCFG_IFG96         (0x03u << 24)  /* only IFG96 is 802.3-clean  */
#define RT_TXCFG_LOOPBACK      ((1u << 18) | (1u << 17))
#define RT_TXCFG_NOCRC         (1u << 16)     /* set = do NOT append FCS    */
#define RT_TXCFG_CLEARABT      (1u << 0)      /* write-only                 */
#define RT_TXCFG_DMA_SHIFT     8
#define RT_TXCFG_RETRY_SHIFT   4

/* --- operational parameters (mirroring 8139too defaults) ------------------ */

#define RT_RX_FIFO_THRESH      7      /* Rx FIFO level before PCI xfer       */
#define RT_RX_DMA_BURST        7      /* max PCI burst ('6' is 1024 B)       */
#define RT_TX_DMA_BURST        6
#define RT_TX_FIFO_THRESH      256    /* bytes; becomes ERTXTH via << 11     */
#define RT_TX_RETRY            8      /* 16 + retry*16 collision attempts    */

/* --- ring geometry -------------------------------------------------------- *
 * The Rx ring is one contiguous DMA region the chip writes around in. With
 * RxNoWrap the chip does not split a frame across the ring end, instead it
 * may run up to RT_RX_RING_WRAP_PAD bytes past the nominal length, so the
 * caller must reserve RT_RX_RING_TOTAL (the 64K ring is a special case:
 * wrapping is unavoidable and RX_BUF_TOT_LEN == 64K).
 */
#define RT_NUM_TX_DESC          4
#define RT_MAX_TX_FRAME         1792   /* chip limit per frame (no jumbo)    */
#define RT_MIN_TX_FRAME         60     /* ETH_ZLEN: the chip has no auto-pad */
#define RT_TX_BOUNCE_SIZE       RT_MAX_TX_FRAME
#define RT_TX_BOUNCE_TOTAL      ((size_t)RT_NUM_TX_DESC * RT_MAX_TX_FRAME)

#define RT_RX_RING_IDX           2                    /* 0=8K 1=16K 2=32K 3=64K */
#define RT_RX_RING_BYTES         (8192u << RT_RX_RING_IDX)
#define RT_RX_RING_PAD           16u
#define RT_RX_RING_WRAP_PAD      2048u
#if RT_RX_RING_BYTES == 65536u
#define RT_RX_RING_TOTAL         RT_RX_RING_BYTES
#else
#define RT_RX_RING_TOTAL         (RT_RX_RING_BYTES + RT_RX_RING_PAD + RT_RX_RING_WRAP_PAD)
#endif

/* each frame in the ring is prefixed by a 4-byte DMA header:
 * status u16 | size u16 (size includes the 4-byte FCS); the frame follows. */
#define RT_RX_HDR_BYTES          4u
#define RT_RX_FCS_BYTES          4u
#define RT_RX_MIN_FRAME          8u    /* smaller sizes mean we lost the ring */
#define RT_RX_FIFO_COPY          0xFFF0u  /* frame copy still in progress     */
#define RT_CAPR_LAG              16u   /* driver writes (cur_rx - 16) to CAPR */

/* --- Cfg9346: 93C46 EEPROM + config-register lock ------------------------- */

#define RT_EE_DATA_READ         0x01
#define RT_EE_DATA_WRITE        0x02
#define RT_EE_SHIFT_CLK         0x04
#define RT_EE_CS                0x08
#define RT_EE_ENB               (0x80 | RT_EE_CS)
#define RT_EE_READ_CMD          6
#define RT_EE_WRITE_CMD         5
#define RT_EE_ERASE_CMD         7
#define RT_CFG9346_LOCK         0x00
#define RT_CFG9346_UNLOCK       0xC0   /* unlock Config0-4 and BMCR writes   */

/* --- CSCR link status ----------------------------------------------------- */

#define RT_CSCR_LINK_OK         0x0400

/* --- driver state --------------------------------------------------------- */

typedef struct {
    uint16_t io;            /* I/O base (BAR0); every register via HAL       */
    uint8_t  mac[6];        /* IDR snapshot, restored on init               */
    uint8_t *rx_ring;       /* RT_RX_RING_TOTAL bytes, DMA-capable, identity-mapped */
    uint8_t *tx_bufs;       /* RT_TX_BOUNCE_TOTAL bytes, DMA-capable        */
    uint32_t cur_rx;        /* host read offset into the Rx ring            */
    uint16_t cur_tx;        /* next Tx descriptor to arm                    */
    uint16_t dirty_tx;      /* oldest descriptor not yet reclaimed          */
    uint32_t tx_flag;       /* ERTXTH value baked into every TSD write      */
    uint32_t rx_config;     /* RCR value to restore after an Rx re-arm     */
    /* monotonic counters (saturation beats wrap-around trickery) */
    uint32_t rx_ok, rx_err, rx_dropped, rx_overflow;
    uint32_t tx_ok, tx_err, tx_ring_full;
} Rtl8139;

/* --- hardware access ------------------------------------------------------ */

/* Probe the I/O window: 0 = an RTL8139-like chip answers, -1 = dead window.
 * io == 0 is always rejected: grandfather ports (PIT at 0x40, PIC at 0x20)
 * must never be mistaken for a NIC. */
int rtl8139_probe(uint16_t io);

/* Soft reset: asserts CR.RST and polls for self-clear, bounded. -1 = the
 * chip never came out of reset. */
int rtl8139_reset(uint16_t io);

/* Read one 16-bit word from the 93C46 EEPROM (3-wire bit-bang through
 * Cfg9346). addr_len is 6 or 8; detect it the way the reference does: if
 * word 0 reads back as 0x8129 the EEPROM uses 8-bit addressing. */
int rtl8139_eeprom_read(uint16_t io, int location, int addr_len);

/* Snapshot IDR0..5 into `out`. */
int rtl8139_read_mac(uint16_t io, uint8_t out[6]);

/* --- bring-up / teardown --------------------------------------------------- *
 * rx_ring and tx_bufs are caller-owned, DMA-capable regions of
 * RT_RX_RING_TOTAL / RT_TX_BOUNCE_TOTAL bytes. The kernel is identity-mapped
 * over the first 16 MiB, so the bus address written into RBSTART/TSAD is the
 * plain pointer value; on ZenithOS the caller must still get those regions
 * from PMM pages, not the 1 MiB reserved window. Returns -1 on bad args or
 * a failed reset.
 */
int rtl8139_init(Rtl8139 *d, uint16_t io, uint8_t *rx_ring, uint8_t *tx_bufs);

void rtl8139_stop(Rtl8139 *d);   /* mask IRQs, stop Tx/Rx DMA */

int rtl8139_link_ok(const Rtl8139 *d);

/* --- interrupt interface (called by the IRQ layer) ------------------------ */

uint16_t rtl8139_int_status(const Rtl8139 *d);  /* raw ISR, masked to ours */
void rtl8139_int_ack(Rtl8139 *d, uint16_t bits); /* RW1C ack + posted-write flush */

/* --- data path ------------------------------------------------------------ */

/*
 * Drain the Rx ring into up to `max` freshly allocated NetBufs (one per
 * frame; the copy out of the DMA ring is unavoidable, the ring is hardware
 * memory). Errors do not abort the whole batch: a malformed frame or a
 * full NetBuf pool is counted and skipped, a lost-in-the-ring condition
 * re-inits the receiver as the reference does. Returns the number of
 * buffers stored into `out` (0..max).
 */
size_t rtl8139_rx(Rtl8139 *d, NetBuf **out, size_t max);

/* Reclaim completed Tx descriptors; returns how many were freed. Call from
 * the TOK/TER interrupt path or before every rtl8139_tx when pooling. */
size_t rtl8139_tx_reclaim(Rtl8139 *d);

/*
 * Queue one frame for transmission. Copies it into the next bounce buffer
 * (the chip demands a DMA-safe, contiguous source and cannot auto-pad),
 * pads to 60 bytes, then arms the descriptor: writing TSD starts the DMA.
 * Returns 0 on success, -1 on invalid frame or a full ring (reclaim first).
 */
int rtl8139_tx(Rtl8139 *d, const void *data, size_t len);

#endif /* ZENITH_RTL8139_H */
