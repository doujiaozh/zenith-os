/* ZenithOS v6.0 - net/ne2000.c - NE2000 / DP8390-compatible NIC driver. */
#include "net/ne2000.h"
#include "hal/x86/port.h"
#include "hal/x86/cpu.h"
#include "lib/string.h"

int ne2000_probe(uint16_t io) {
    if (io == 0 || io == 0x40 || (io & 0xFF80) == 0x170) return -1;
    /* A live NE2000 answers a page-0 register read with something sane;
     * dead windows return 0xFF everywhere. */
    if (inb(io + NE_PSTART) == 0xFF && inb(io + NE_PSTOP) == 0xFF) return -1;
    return 0;
}

int ne2000_init(Ne2000 *d, uint16_t io) {
    int i;

    if (d == NULL || io == 0) return -1;
    kmemset(d, 0, sizeof(*d));
    d->io = io;

    /* stop on page 0 so the configuration writes are stable */
    outb(io + NE_CR, NE_CR_STP | NE_CR_PAGE0);

    outb(io + NE_DCR, 0x48);   /* 8-bit data port (v86 honors bit 0 only) */
    outb(io + NE_RCR, 0x04);   /* accept broadcast; unicast-to-mac is default */
    outb(io + NE_TCR, 0x00);   /* normal transmit */

    outb(io + NE_PSTART, NE_RING_START);
    outb(io + NE_PSTOP, NE_RING_STOP);
    outb(io + NE_BNRY, NE_RING_START);

    /* CURR lives on page 1 (offset 7) */
    outb(io + NE_CR, NE_CR_STP | NE_CR_PAGE1);
    outb(io + NE_ISR + 0, NE_RING_START);      /* CURR = ring start */
    for (i = 0; i < 6; i++) d->mac[i] = inb(io + NE_PSTART + (uint16_t)i);
    outb(io + NE_CR, NE_CR_STP | NE_CR_PAGE0);

    outb(io + NE_ISR, 0xFF);   /* clear any pending status */
    outb(io + NE_IMR, NE_ISR_RX_OK | NE_ISR_TX_OK | NE_ISR_RDC);

    outb(io + NE_CR, NE_CR_STA | NE_CR_PAGE0);  /* running: receive enabled */
    return 0;
}

int ne2000_send(Ne2000 *d, const uint8_t *frame, int len) {
    uint16_t base = (uint16_t)NE_TX_PAGE << 8;
    int i;

    if (d == NULL || frame == NULL || len <= 0 || len > 1514) return -1;

    outb(d->io + NE_CR, NE_CR_STA | NE_CR_PAGE0);
    /* remote DMA write into the chip's buffer RAM at the Tx staging page */
    outb(d->io + NE_RSAR0, (uint8_t)(base & 0xFF));
    outb(d->io + NE_RSAR1, (uint8_t)(base >> 8));
    outb(d->io + NE_RBCR0, (uint8_t)(len & 0xFF));
    outb(d->io + NE_RBCR1, (uint8_t)(len >> 8));
    for (i = 0; i < len; i++) outb(d->io + NE_DATA, frame[i]);

    /* arm the transmit descriptor and pulse TXP */
    outb(d->io + NE_TPSR, NE_TX_PAGE);
    outb(d->io + NE_TBCR0, (uint8_t)(len & 0xFF));
    outb(d->io + NE_TBCR1, (uint8_t)(len >> 8));
    outb(d->io + NE_CR, NE_CR_STA | NE_CR_PAGE0 | NE_CR_TXP);
    return 0;
}

int ne2000_recv(Ne2000 *d, uint8_t *buf, int cap) {
    uint8_t bnry, curr, hdr[4];
    uint16_t count, n;
    uint16_t addr;
    int i;

    if (d == NULL || buf == NULL || cap <= 0) return 0;

    bnry = inb(d->io + NE_BNRY);
    outb(d->io + NE_CR, NE_CR_STA | NE_CR_PAGE1);
    curr = inb(d->io + NE_ISR);          /* page-1 offset 7 reads CURR */
    outb(d->io + NE_CR, NE_CR_STA | NE_CR_PAGE0);
    if (bnry == curr) return 0;          /* ring empty */

    /* 4-byte header at the boundary page, via remote DMA read */
    addr = (uint16_t)bnry << 8;
    outb(d->io + NE_RSAR0, (uint8_t)(addr & 0xFF));
    outb(d->io + NE_RSAR1, (uint8_t)(addr >> 8));
    outb(d->io + NE_RBCR0, 4);
    outb(d->io + NE_RBCR1, 0);
    for (i = 0; i < 4; i++) hdr[i] = inb(d->io + NE_DATA);

    count = (uint16_t)(hdr[2] | (hdr[3] << 8));   /* includes the 4-byte FCS */
    if (count < 8 || count > 1518 + 4) {
        /* header looks wrong: reset the boundary, drop this frame */
        outb(d->io + NE_BNRY, inb(d->io + NE_PSTOP) - 1);
        return 0;
    }
    n = (uint16_t)(count - 4);                    /* bytes of frame on wire */
    if ((int)n > cap) n = (uint16_t)cap;

    addr = (uint16_t)((bnry << 8) + 4);
    outb(d->io + NE_RSAR0, (uint8_t)(addr & 0xFF));
    outb(d->io + NE_RSAR1, (uint8_t)(addr >> 8));
    outb(d->io + NE_RBCR0, (uint8_t)(count - 4));
    outb(d->io + NE_RBCR1, 0);
    for (i = 0; i < n; i++) buf[i] = inb(d->io + NE_DATA);

    /* publish the boundary past this frame; wrap into the ring */
    bnry = hdr[1];
    if (bnry < NE_RING_START || bnry >= NE_RING_STOP) bnry = NE_RING_START;
    outb(d->io + NE_BNRY, bnry);

    outb(d->io + NE_ISR, NE_ISR_RX_OK | NE_ISR_RDC);
    return (int)n;
}

uint8_t ne2000_int_status(Ne2000 *d) {
    if (d == NULL || d->io == 0) return 0;
    outb(d->io + NE_CR, NE_CR_STA | NE_CR_PAGE0);
    return inb(d->io + NE_ISR);
}

void ne2000_int_ack(Ne2000 *d, uint8_t bits) {
    if (d == NULL || d->io == 0) return;
    outb(d->io + NE_ISR, bits);
}
