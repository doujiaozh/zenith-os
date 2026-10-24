/* ZenithOS v6.0 - PCI bus driver: configuration space + bus scan.
 *
 * Type-1 configuration cycles through 0xCF8/0xCFC: write the address with
 * the enable bit, read/write the dword at the data port. Every access goes
 * through the HAL (outl/inl are the only port entry into the kernel).
 *
 * The scan covers bus 0 (the single PCI bus of the i440fx-class machines
 * ZenithOS targets); multi-function devices are followed via header-type
 * bit 7. Emulators without a PCI host bridge (v86) answer reads with
 * 0x0000/0xFFFF, so the scan simply finds nothing and everyone degrades
 * gracefully.
 */
#ifndef ZENITH_PCI_H
#define ZENITH_PCI_H

#include <stdint.h>
#include "zenith.h"

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC
#define PCI_VENDOR_NONE  0xFFFFu

/* PCI standard header registers (dword offsets) */
#define PCI_REG_VENDOR   0x00    /* vendor id | device id                    */
#define PCI_REG_COMMAND  0x04    /* command: IO / memory / bus-master bits  */
#define PCI_REG_CLASS    0x08    /* class code | subclass | prog if | rev   */
#define PCI_REG_BAR0     0x10    /* first base address register             */
#define PCI_REG_IRQLINE  0x3C    /* interrupt line | interrupt pin          */
#define PCI_REG_HEADERTYPE 0x0E  /* byte 14 inside dword 0x0C               */

#define PCI_CMD_IO       0x01u
#define PCI_CMD_MEM      0x02u
#define PCI_CMD_BUSMASTER 0x04u

#define PCI_MAX_FOUND    32

typedef struct {
    uint8_t  bus, dev, func;
    uint16_t vendor;
    uint16_t device;
    uint8_t  irq_line;
    uint32_t bar0;
} PciDevice;

/* Scan bus 0 and fill the internal device table. Safe to call once at boot;
 * also fine on machines without PCI (the table just stays empty). */
void pci_init(void);

/* Number of devices found by the last scan. */
int pci_count(void);

/* Look up a device by vendor/device id; returns NULL when absent. */
const PciDevice *pci_find(uint16_t vendor, uint16_t device);

/* Raw configuration space access (address = dword register offset). */
uint32_t pci_cfg_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
void     pci_cfg_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg,
                       uint32_t value);

#endif /* ZENITH_PCI_H */
