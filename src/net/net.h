/* ZenithOS v6.0 - net/net.h - network bring-up orchestration.
 *
 * The layers stay separate on purpose (mechanism below, policy here):
 *   drivers/pci.c    - bus enumeration, hands us an I/O base + IRQ line
 *   mm/pmm.c         - the DMA regions, above 1 MiB, identity mapped
 *   net/rtl8139.c    - the register-level NIC driver (vendor code)
 *   net/netbuf.c     - the packet buffer pool the Rx path drains into
 * This module decides *when* they meet: net_probe() after PMM is up (it
 * needs physical pages for the DMA rings), net_attach_irq() after the IDT
 * and PIC are remapped (it needs a vector to bind), then net_arp_exchange()
 * proves both directions of the wire.
 */
#ifndef ZENITH_NET_H
#define ZENITH_NET_H

#include <stdint.h>

/* Scan the PCI bus, find the RTL8139, allocate DMA-able pages from PMM
 * (above 1 MiB, inside the identity-mapped window so bus address == pointer)
 * and bring the chip up. No IRQ binding yet: call net_attach_irq() once the
 * IDT/PIC live. Safe on machines without PCI or without the NIC: logs a
 * single line and leaves the subsystem down. */
void net_probe(void);

/* Bind the NIC interrupt (the line the PCI interrupt-pin register resolves
 * to, as programmed by the BIOS) and unmask it at the PIC. Falls back to
 * pure polling when the line is missing or unusable. */
void net_attach_irq(void);

/* Send one ARP request for the QEMU user-net gateway (10.0.2.2) and poll
 * the Rx ring for up to ~2 s. Every step lands on the serial log, which is
 * how the CI QEMU job asserts a packet was actually received. */
int net_arp_exchange(void);

/* One-line status for the shell. */
void net_status(char *buf, int cap);

#endif /* ZENITH_NET_H */
