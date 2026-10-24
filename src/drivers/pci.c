/* ZenithOS v6.0 - PCI bus driver implementation. */
#include "drivers/pci.h"
#include "hal/x86/port.h"
#include "lib/fmt.h"

static PciDevice found[PCI_MAX_FOUND];
static int nfound = 0;

uint32_t pci_cfg_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8) | ((uint32_t)reg & 0xFCu);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_cfg_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg,
                   uint32_t value) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8) | ((uint32_t)reg & 0xFCu);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

/* One function of one device; returns 1 when a device lives there. */
static int pci_probe_func(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t vd = pci_cfg_read(bus, dev, func, PCI_REG_VENDOR);
    uint16_t vendor = (uint16_t)(vd & 0xFFFFu);
    uint16_t device = (uint16_t)(vd >> 16);
    uint32_t cmd;
    uint32_t bar0;

    if (vendor == 0xFFFFu || vendor == 0) return 0;
    if (nfound >= PCI_MAX_FOUND) return 0;

    /* enable I/O + memory decode and bus mastering: a NIC that cannot
     * master the bus cannot DMA frames into our ring. */
    cmd = pci_cfg_read(bus, dev, func, PCI_REG_COMMAND);
    pci_cfg_write(bus, dev, func, PCI_REG_COMMAND,
                  cmd | PCI_CMD_IO | PCI_CMD_MEM | PCI_CMD_BUSMASTER);

    bar0 = pci_cfg_read(bus, dev, func, PCI_REG_BAR0);

    found[nfound].bus = bus;
    found[nfound].dev = dev;
    found[nfound].func = func;
    found[nfound].vendor = vendor;
    found[nfound].device = device;
    found[nfound].irq_line =
        (uint8_t)(pci_cfg_read(bus, dev, func, PCI_REG_IRQLINE) & 0xFFu);
    found[nfound].bar0 = bar0 & 0xFFFFFFF8u;   /* low bits are flags */
    nfound++;

    klog("pci: %02x:%02x.%x vendor %04x device %04x irq %u io 0x%x\r\n",
         bus, dev, func, vendor, device,
         found[nfound - 1].irq_line, found[nfound - 1].bar0);
    return 1;
}

void pci_init(void) {
    uint8_t dev, func;

    for (dev = 0; dev < 32; dev++) {
        /* function 0: present on every single-function device */
        if (!pci_probe_func(0, dev, 0))
            continue;
        /* multi-function? bit 7 of header type (byte 14 = reg 0x0C, bit 8..15) */
        {
            uint32_t ht = pci_cfg_read(0, dev, 0, PCI_REG_HEADERTYPE);
            if (ht & 0x00008000u) {
                for (func = 1; func < 8; func++)
                    pci_probe_func(0, dev, func);
            }
        }
    }
    if (nfound == 0)
        klog("pci: no devices found (PCI host bridge absent?)\r\n");
    else
        klog("pci: %d device(s) probed, bus-mastering enabled\r\n", nfound);
}

int pci_count(void) { return nfound; }

const PciDevice *pci_find(uint16_t vendor, uint16_t device) {
    int i;
    for (i = 0; i < nfound; i++) {
        if (found[i].vendor == vendor && found[i].device == device)
            return &found[i];
    }
    return NULL;
}
