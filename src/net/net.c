/* ZenithOS v6.0 - net/net.c - network bring-up orchestration.
 *
 * One probe, two NICs: the RTL8139 (QEMU/real hardware) and the NE2000-class
 * RTL8029 that v86 emulates. Both are reached through the same per-driver
 * send/recv dispatch, so the ARP round-trip code below is NIC-agnostic:
 *
 *   drivers/pci.c   - bus enumeration, hands us I/O base + IRQ line
 *   mm/pmm.c        - the RTL8139 DMA rings (above 1 MiB, identity mapped)
 *   net/rtl8139.c   - the QEMU/real-hw NIC register driver
 *   net/ne2000.c    - the v86 NIC register driver (ring in chip RAM)
 *   net/netbuf.c    - the packet buffer pool the RTL8139 Rx path drains into
 */
#include "net/net.h"
#include "net/rtl8139.h"
#include "net/ne2000.h"
#include "net/netbuf.h"
#include "drivers/pci.h"
#include "drivers/pit.h"
#include "arch/x86_32/isr.h"
#include "arch/x86_32/pic.h"
#include "mm/pmm.h"
#include "hal/x86/cpu.h"
#include "lib/fmt.h"
#include "lib/string.h"

/* QEMU user-mode network constants: the guest sits at 10.0.2.15 and the
 * SLIRP backend answers ARP for the 10.0.2.2 gateway - that reply is the
 * cheapest full round-trip proof. The v86 NE2000 has no such backend, so
 * the exchange there proves the Tx path and times out cleanly. */
#define NET_guest_IP  {10, 0, 2, 15}
#define NET_gw_IP     {10, 0, 2, 2}
#define NET_gw_MAC    {0x52, 0x54, 0x00, 0x12, 0x34, 0x56}

enum { NIC_NONE = 0, NIC_RTL8139 = 1, NIC_NE2000 = 2 };

static int nic_type = NIC_NONE;
static uint8_t nic_irq = 0xFF;        /* 0xFF = poll-only */
static uint8_t our_mac[6];
static volatile int rx_flag = 0;      /* set by the IRQ path, drained by poll */

static Rtl8139 rtl;
static Ne2000 ne2k;

/* --- per-driver dispatch -------------------------------------------------- */

static int nic_send(const uint8_t *frame, int len) {
    if (nic_type == NIC_RTL8139)
        return rtl8139_tx(&rtl, frame, (size_t)len) == 0 ? 0 : -1;
    if (nic_type == NIC_NE2000)
        return ne2000_send(&ne2k, frame, len);
    return -1;
}

static int nic_recv(uint8_t *buf, int cap) {
    if (nic_type == NIC_RTL8139) {
        NetBuf *nb = NULL;
        int n;

        if (rtl8139_rx(&rtl, &nb, 1) == 0 || nb == NULL) return 0;
        n = (int)netbuf_len(nb);
        if (n > cap) n = cap;
        kmemcpy(buf, netbuf_data(nb), (size_t)n);
        netbuf_free(nb);
        rtl8139_tx_reclaim(&rtl);
        return n;
    }
    if (nic_type == NIC_NE2000)
        return ne2000_recv(&ne2k, buf, cap);
    return 0;
}

static void nic_irq_handler_rtl(void) {
    uint16_t st = rtl8139_int_status(&rtl);
    if (st & RT_ISR_RX_ACK) rx_flag = 1;
    rtl8139_int_ack(&rtl, st);
}

static void nic_irq_handler_ne2k(void) {
    uint8_t st = ne2000_int_status(&ne2k);
    if (st & NE_ISR_RX_OK) rx_flag = 1;
    ne2000_int_ack(&ne2k, st);
}

/* n physically contiguous 4 KiB pages above 1 MiB (RTL8139 DMA rings). The
 * PMM bitmap is a first-fit scan, so back-to-back allocations yield a
 * contiguous run; the identity map covers the low 16 MiB, so the bus
 * address equals the pointer and the chip can DMA straight at it. */
static void *pmm_alloc_pages(int n) {
    int i;
    uint8_t *base = NULL;

    for (i = 0; i < n; i++) {
        uint32_t page = pmm_alloc_page();
        if (page == 0) {
            klog("net: pmm exhausted while allocating %d pages\r\n", n);
            return NULL;
        }
        if (base == NULL) base = (uint8_t *)page;
        else if ((uint8_t *)page != base + (size_t)i * 4096) {
            klog("net: pmm gave a non-contiguous run\r\n");
            return NULL;
        }
    }
    return base;
}

void net_probe(void) {
    netbuf_init();
    pci_init();

    {
        const PciDevice *dev =
            pci_find(RTL8139_PCI_VENDOR_ID, RTL8139_PCI_DEVICE_ID);
        uint16_t io;
        uint8_t *rx_ring, *tx_bufs;

        if (dev != NULL) {
            io = (uint16_t)(dev->bar0 & 0xFFFCu);
            if (io == 0) {
                klog("net: rtl8139 BAR0 is not an I/O bar\r\n");
                return;
            }
            rx_ring = (uint8_t *)pmm_alloc_pages(
                (int)((RT_RX_RING_TOTAL + 4095) / 4096));
            tx_bufs = (uint8_t *)pmm_alloc_pages(
                (int)((RT_TX_BOUNCE_TOTAL + 4095) / 4096));
            if (rx_ring == NULL || tx_bufs == NULL) return;
            if (rtl8139_init(&rtl, io, rx_ring, tx_bufs) != 0) {
                klog("net: rtl8139 init failed at io 0x%x\r\n", io);
                return;
            }
            kmemcpy(our_mac, rtl.mac, 6);
            klog("net: rtl8139 up io 0x%x mac %02x:%02x:%02x:%02x:%02x:%02x "
                 "rxring 0x%x tx 0x%x\r\n",
                 io, our_mac[0], our_mac[1], our_mac[2],
                 our_mac[3], our_mac[4], our_mac[5],
                 (unsigned)(uintptr_t)rx_ring, (unsigned)(uintptr_t)tx_bufs);
            nic_irq = dev->irq_line;
            nic_type = NIC_RTL8139;
            return;
        }
    }

    {
        const PciDevice *dev =
            pci_find(NE2000_PCI_VENDOR_ID, NE2000_PCI_DEVICE_ID);
        uint16_t io;

        if (dev == NULL) {
            klog("net: no network card on the PCI bus "
                 "(looked for rtl8139 and ne2000/rtl8029)\r\n");
            return;
        }
        io = (uint16_t)(dev->bar0 & 0xFFFCu);
        if (io == 0) {
            klog("net: ne2000 BAR0 is not an I/O bar\r\n");
            return;
        }
        if (ne2000_init(&ne2k, io) != 0) {
            klog("net: ne2000 init failed at io 0x%x\r\n", io);
            return;
        }
        kmemcpy(our_mac, ne2k.mac, 6);
        klog("net: ne2000 up io 0x%x mac %02x:%02x:%02x:%02x:%02x:%02x "
             "ring %u..%u\r\n",
             io, our_mac[0], our_mac[1], our_mac[2],
             our_mac[3], our_mac[4], our_mac[5],
             (unsigned)NE_RING_START, (unsigned)NE_RING_STOP);
        nic_irq = dev->irq_line;
        nic_type = NIC_NE2000;
    }
}

void net_attach_irq(void) {
    if (nic_type == NIC_NONE) return;
    if (nic_irq == 0 || nic_irq >= 16) {
        klog("net: irq line %u unusable, staying polled\r\n", nic_irq);
        nic_irq = 0xFF;
        return;
    }
    if (nic_type == NIC_RTL8139)
        irq_install_handler(nic_irq, nic_irq_handler_rtl);
    else
        irq_install_handler(nic_irq, nic_irq_handler_ne2k);
    pic_unmask(nic_irq);
    klog("net: IRQ %u routed and unmasked\r\n", nic_irq);
}

/* --- ARP: the cheapest full round trip ----------------------------------- *
 * 14 B Ethernet header + 28 B ARP payload = 42 B; both NIC drivers pad.
 */
static void build_arp(uint8_t *frame) {
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t guest_ip[4] = NET_guest_IP;
    static const uint8_t gw_ip[4] = NET_gw_IP;
    int i;

    for (i = 0; i < 6; i++) frame[i] = bcast[i];        /* dst: broadcast */
    for (i = 0; i < 6; i++) frame[6 + i] = our_mac[i];  /* src: us         */
    frame[12] = 0x08; frame[13] = 0x06;                 /* ethertype: ARP  */

    frame[14] = 0x00; frame[15] = 0x01;                 /* htype: ethernet */
    frame[16] = 0x08; frame[17] = 0x00;                 /* ptype: IPv4     */
    frame[18] = 6;                                      /* hlen            */
    frame[19] = 4;                                      /* plen            */
    frame[20] = 0x00; frame[21] = 0x01;                 /* op: request     */
    for (i = 0; i < 6; i++) frame[22 + i] = our_mac[i];
    for (i = 0; i < 4; i++) frame[28 + i] = guest_ip[i];
    for (i = 0; i < 6; i++) frame[32 + i] = 0;          /* target hardware */
    for (i = 0; i < 4; i++) frame[38 + i] = gw_ip[i];   /* target protocol */
}

static int frame_is_arp_reply(const uint8_t *f, int len) {
    if (len < 42) return 0;
    if (f[12] != 0x08 || f[13] != 0x06) return 0;       /* ethertype ARP */
    if (f[20] != 0x00 || f[21] != 0x02) return 0;       /* op: reply     */
    return 1;
}

int net_arp_exchange(void) {
    uint8_t arp[42], frame[1600];
    uint32_t deadline;
    int rc = -1;

    if (nic_type == NIC_NONE) return -1;

    build_arp(arp);
    if (nic_send(arp, sizeof(arp)) != 0) {
        klog("net: nic_send failed\r\n");
        return -1;
    }
    klog("net: arp who-has 10.0.2.2 tell 10.0.2.15 sent (%u B)\r\n",
         (unsigned)sizeof(arp));

    deadline = pit_ticks() + 200;      /* 2 s at 100 Hz */
    while ((int32_t)(deadline - pit_ticks()) > 0) {
        if (rx_flag) rx_flag = 0;      /* edge from the IRQ path */
        for (;;) {
            int len = nic_recv(frame, sizeof(frame));
            if (len <= 0) break;
            klog("net: rx %d bytes%s\r\n", len,
                 frame_is_arp_reply(frame, len) ? "  ARP-REPLY ok" : "");
            if (frame_is_arp_reply(frame, len)) {
                klog("net: gateway 10.0.2.2 is at %02x:%02x:%02x:%02x:%02x:%02x\r\n",
                     frame[22], frame[23], frame[24], frame[25],
                     frame[26], frame[27]);
                rc = 0;
            }
        }
        if (rc == 0) break;
        cpu_hlt();                     /* an IRQ or the 100 Hz tick wakes us */
    }
    if (rc != 0)
        klog("net: arp exchange timed out (no reply in 2 s)\r\n");
    return rc;
}

void net_status(char *buf, int cap) {
    const char *s = nic_type != NIC_NONE
                        ? "net: nic up (see serial log)"
                        : "net: no nic";
    int i;

    for (i = 0; i + 1 < cap && s[i]; i++) buf[i] = s[i];
    buf[i] = 0;
}
