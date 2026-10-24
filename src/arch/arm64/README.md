# ZenithOS arm64 bring-up (interface skeleton)

`src/arch/arm64/` defines the interface the kernel expects from an arm64
architecture. It is **not** compiled in v6.0 — v6.0 boots only x86 (IA-32).
This directory exists so that `src/arch/` expresses the "multi-architecture"
requirement of the constitution purely through interfaces, and so a future
port slots in without changing kernel code.

## Boot contract
- A boot shim (EL3/EL2 -> EL1) hands the kernel a `BootInfo` block at a fixed
  DRAM address, identical in shape to the x86 one (`src/include/bootinfo.h`),
  with `boot_type = BOOT_TYPE_UEFI` and `video_mode` describing GOP vs serial.
- Kernel runs at EL1 with: MMU off (kernel enables 4 KiB-granule identity map),
  D-cache off until mapped, stack set by the shim.

## Required primitive providers
| Interface        | arm64 realization                          |
|------------------|--------------------------------------------|
| `arch_early_init`| configure MAIR_EL1/TCR_EL1/TTBR0_EL1, install vector table, init GICv3 + generic timer |
| `arch_halt`      | `wfi` loop                                  |
| port I/O         | mapped MMIO load/stores (`mmio_read32/write32`) |
| paging           | 4-level, 4 KiB granules, identity map low DRAM |

## Porting checklist
1. Write `startup.S` (EL1 entry, bss zero, call `kernel_main`).
2. Implement `gdt/idt/pic` equivalents as `vector.c` + GIC driver.
3. Implement `pit` equivalent using the ARM generic timer.
4. Implement `keyboard`/`serial` over PL011 + a USB/PCI keyboard driver.
5. Provide `vga` via the UEFI linear framebuffer (GOP) or a PL111/etnaviv device.
