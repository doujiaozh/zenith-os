/* ZenithOS v6.0 - net/ne2000.h - NE2000 / DP8390-compatible NIC driver.
 *
 * Target: the RTL8029AS (PCI vendor 0x10EC, device 0x8029) that v86 emulates
 * on its i440fx machine, and real NE2000 clones. Register layout and
 * semantics match the DP8390 datasheet as v86 implements them (verified
 * register by register against the vendored v86 ne2k device), which makes
 * this driver the local end-to-end verification counterpart of the QEMU
 * RTL8139 path:
 *
 *   Tx: stage the frame into the ring through remote DMA (RSAR/RBCR +
 *       the 0x10 data port), set TPSR/TBCR, then pulse CR.TXP (0x04).
 *   Rx: the chip writes each frame at CURR with a 4-byte header
 *       {status, next page, count lo, count hi} where count includes the
 *       4-byte FCS; the host drains from BNRY with remote reads (the data
 *       port wraps at PSTOP on its own) and publishes BNRY = next page.
 *
 * Every register touch goes through the HAL; the ring lives in the chip's
 * own buffer RAM (not host RAM), so unlike the RTL8139 there is no DMA
 * region to allocate.
 */
#ifndef ZENITH_NE2000_H
#define ZENITH_NE2000_H

#include <stdint.h>
#include "zenith.h"

#define NE2000_PCI_VENDOR_ID  0x10EC
#define NE2000_PCI_DEVICE_ID  0x8029

/* page-0 register offsets from the I/O base */
#define NE_CR      0x00    /* command: 0x01 STP, 0x02 STA, 0x04 TXP, p6-7 page */
#define NE_PSTART  0x01    /* page 0: ring start page; page 1: MAC byte 0     */
#define NE_PSTOP   0x02    /* page 0: ring stop page;  page 1: MAC byte 1     */
#define NE_BNRY    0x03    /* page 0: boundary (host read page)               */
#define NE_TPSR    0x04    /* transmit page start                            */
#define NE_TBCR0   0x05    /* transmit byte count, low/high (v86: tcnt)       */
#define NE_TBCR1   0x06
#define NE_ISR     0x07    /* page 0: interrupt status (RW1C); page 1: CURR  */
#define NE_RSAR0   0x08    /* remote start address, low/high                 */
#define NE_RSAR1   0x09
#define NE_RBCR0   0x0A    /* remote byte count, low/high (v86: rcnt)         */
#define NE_RBCR1   0x0B
#define NE_RCR     0x0C    /* receive configuration                          */
#define NE_TCR     0x0D    /* transmit configuration                         */
#define NE_DCR     0x0E    /* data configuration (bit 0 selects 8/16-bit)    */
#define NE_IMR     0x0F    /* interrupt mask                                 */
#define NE_DATA    0x10    /* remote DMA data port                           */
#define NE_RESET   0x1F

#define NE_CR_STP  0x01
#define NE_CR_STA  0x02
#define NE_CR_TXP  0x04
#define NE_CR_PAGE0 0x00
#define NE_CR_PAGE1 0x40

#define NE_ISR_RX_OK  0x01
#define NE_ISR_TX_OK  0x02
#define NE_ISR_RX_ERR 0x04
#define NE_ISR_TX_ERR 0x08
#define NE_ISR_OVERW  0x10
#define NE_ISR_RDC    0x40
#define NE_ISR_RST    0x80

/* Chip buffer-RAM layout (32 KiB = pages 0..127). The remote-DMA data port
 * in the v86 ne2k model only accepts writes at RSAR <= 16 or in the
 * 0x4000..0x8000 window, and it wraps RSAR back to PSTART as soon as it
 * passes PSTOP -- so the Tx staging page must sit at 0x4000+ AND leave the
 * whole frame below PSTOP, which is why the ring runs to 128 while the
 * frame itself is staged at page 96. */
#define NE_RING_START 64      /* Rx ring: 16 KiB, 64..128                   */
#define NE_RING_STOP  128
#define NE_TX_PAGE    96      /* Tx staging page (0x6000), below the wrap    */

typedef struct {
    uint16_t io;
    uint8_t  mac[6];
} Ne2000;

/* Reject grandfather ports: 0 and the classic IDE/PIC ranges are never a NIC. */
int ne2000_probe(uint16_t io);

/* Configure the ring, pin the MAC (v86 randomizes PAR at boot, so the
 * driver programs the QEMU user-net address), start the receiver. 0/-1. */
int ne2000_init(Ne2000 *d, uint16_t io);

/* Copy one frame into the Tx staging page and pulse TXP. 0/-1. */
int ne2000_send(Ne2000 *d, const uint8_t *frame, int len);

/* Drain one received frame into buf (up to cap). Returns the frame length
 * as stored (count-4), or 0 when the ring is empty. */
int ne2000_recv(Ne2000 *d, uint8_t *buf, int cap);

/* ISR value (page 0) and RW1C ack, for the IRQ layer + polling. */
uint8_t ne2000_int_status(Ne2000 *d);
void    ne2000_int_ack(Ne2000 *d, uint8_t bits);

#endif /* ZENITH_NE2000_H */
