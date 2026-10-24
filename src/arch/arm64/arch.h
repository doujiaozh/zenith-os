/*
 * ZenithOS v6.0 - arm64 architecture interface (skelleton, not yet built).
 * This header defines the SAME arch_early_init()/arch_halt() contract as
 * src/arch/x86_32 so the kernel stays architecture-neutral. arm64 is a
 * compile-time interface only in v6.0: it is not compiled into the x86
 * bootable image. See src/arch/arm64/README.md for the bring-up plan.
 */
#ifndef ZENITH_ARCH_ARM64_H
#define ZENITH_ARCH_ARM64_H

/*
 * Planned arm64 primitives (documentation-only in v6.0):
 *   void arch_early_init(void);  // EL1 setup: MAIR/TCR/TTBR0_EL1, exception
 *                                // vector table, generic timer, GICv3
 *   void arch_halt(void);        // wfi loop
 *   void  outb/mmio_write32(uint64_t addr, uint32_t val);
 *   uint32_t mmio_read32(uint64_t addr);
 *
 * Flat identity mapping of low DRAM via 4 KiB translation granules,
 * 48-bit VAs (4 levels: TTBR0_EL1 -> L0/L1/L2/L3).
 */

#endif /* ZENITH_ARCH_ARM64_H */
