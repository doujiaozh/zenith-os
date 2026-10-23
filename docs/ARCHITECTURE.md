# ZenithOS Architecture

> **Status: v1.0 "Neural Core".** A 32-bit x86, single-address-space, ring-0
> operating system booting from a raw 1.44 MB floppy image, rendering a
> 320×200×256 neon neural desktop driven by an in-kernel spiking neural
> network. This document describes **what the source actually does** — module
> by module, with the real constants, addresses, and algorithms. Where a
> capability is absent, it says so.

Related documents: [`README.md`](../README.md) for the overview,
[`AI-NATIVE.md`](AI-NATIVE.md) for the model rationale,
[`SECURITY.md`](SECURITY.md) for the threat model,
[`TESTING.md`](TESTING.md) for verification.

---

## Table of contents

- [1. System shape](#1-system-shape)
- [2. Boot sequence](#2-boot-sequence)
- [3. Physical memory map](#3-physical-memory-map)
- [4. Module interfaces](#4-module-interfaces)
- [5. Interrupt path](#5-interrupt-path)
- [6. PIC and PIT](#6-pic-and-pit)
- [7. Keyboard](#7-keyboard)
- [8. Physical memory allocator (PMM)](#8-physical-memory-allocator-pmm)
- [9. Kernel heap](#9-kernel-heap)
- [10. The spiking neural network](#10-the-spiking-neural-network)
- [11. Rendering pipeline](#11-rendering-pipeline)
- [12. Palette and DAC](#12-palette-and-dac)
- [13. Unified logging](#13-unified-logging)
- [14. Panic path](#14-panic-path)
- [15. System-call ABI](#15-system-call-abi)
- [16. Boot self-tests](#16-boot-self-tests)
- [17. Build pipeline](#17-build-pipeline)
- [18. Headless verification harness](#18-headless-verification-harness)
- [19. Determinism and invariants](#19-determinism-and-invariants)
- [20. Source index](#20-source-index)

---

## 1. System shape

| Property | v1.0 value |
| --- | --- |
| CPU mode | 32-bit protected mode, flat 4 GiB segments, ring 0 only |
| Boot medium | raw 1.44 MB floppy image, LBA 0 = boot sector |
| Boot sector | 512 bytes, real mode, ends in `0xAA55` |
| Kernel load | physical `0x10000`, 120 CHS sectors = 61,440 bytes max |
| Video | VGA mode 13h, 320×200, 256 colours, framebuffer `0xA0000` |
| Display buffer | 64,000-byte off-screen back buffer at `0x20000` |
| Timer | 8254 PIT channel 0, 100 Hz, divisor 11,932 |
| Input | 8042 keyboard controller, IRQ1, scancode set 1 |
| Serial | COM1 (`0x3F8`), 115,200 8N1, polled output |
| Clock | CMOS RTC, read-only |
| AI | 24-neuron deterministic spiking network in kernel space |
| Memory | E820-driven 4 KiB page bitmap + static free-list heap |
| Userspace | **none** (no ring 3, no paging, no processes) |
| Dependencies | zero at runtime; Node + NASM at build time only |

`src/include/config.inc` is the single source of truth for every address and
constant shared between the boot sector and the kernel. Both `boot.asm` and
`src/kernel.asm` include it, which is how the loader and the kernel are kept
from drifting apart.

### 1.1 Kernel composition

`src/kernel.asm` is the only translation unit that is assembled into
`kernel.bin`. It `%include`s every module in link order, then defines
`kernel_entry`. Each module is *also* assembled separately by the build with
`-DZENITH_OBJ_CHECK`, which forces every cross-module dependency to be
declared in an `%ifdef` block. The include order is dependency order:

```text
lib/string  lib/fixed
drivers/serial
arch/gdt  arch/pic  arch/idt  arch/isr  arch/syscall  lib/panic
drivers/pit  drivers/rtc  drivers/keyboard
drivers/vga
ui/font  ui/theme
mm/pmm  mm/heap
ai/snn
lib/log  lib/selftest
ui/neural
kernel_entry (this file)
```

The flat image starts with a short `jmp kernel_entry` at offset 0 because the
boot sector jumps to `KERNEL_ORG`, the first byte of the image; `kernel_entry`
itself is placed after the modules so the file reads like a link map. This is an
implementation detail, not a stable ABI.

---

## 2. Boot sequence

The boot sector is 512 bytes of real-mode code, assembled with
`nasm -f bin -I src/ boot.asm -o build/boot.bin`. It does **not** parse a
filesystem: the image is raw, the kernel starts at LBA 1, and there is no BPB
(its 59 bytes are spent on the E820 probe and the per-sector retry path).

| # | Step | Code | Details |
| --- | --- | --- | --- |
| 1 | Firmware loads LBA 0 | — | BIOS copies the sector to `0000:7C00` and jumps to it |
| 2 | Real-mode setup | `boot_main` | `CLI`, `DS=ES=SS=0`, `SP=0x7C00`, `CLD`, save `DL` (boot drive) |
| 3 | A20 enable | `enable_a20` | Test first; then BIOS `INT 15h AX=2401h`; then fast A20 (port `0x92` bit 1). Each attempt is verified by the `0x100500` vs `0x000500` alias test |
| 4 | E820 probe | `e820_probe` | `INT 15h EAX=E820 EDX='SMAP' ECX=24`, up to 8 entries written to `0x0510`, count stored at `0x0506`, magic/version at `0x0500`/`0x0504` |
| 5 | Kernel load | `load_kernel` | One 512-byte sector per `INT 13h AH=02h` call into `ES = KERNEL_ORG>>4`, `BX = dest_off`; walk cylinder 0/head 0/sector 2 → sector 18, then head 1, then next cylinder; 3 attempts per sector with controller reset between failures |
| 6 | VGA mode | `boot_main` | `INT 10h AX=0013h` — mode 13h selected **before** leaving real mode, so no BIOS call is ever needed in 32-bit code |
| 7 | Boot info | `boot_main` | Rewrite `[0x0500]=0x4F4E455A` ('ZENO'), `[0x0504]=1`, `[0x0508]=boot_drive` (the E820 count at `+6` survives from step 4) |
| 8 | Protected mode | `boot_main` | `CLI`, `LGDT`, set `CR0.PE`, far jump `0x08:pm_entry` |
| 9 | 32-bit trampoline | `pm_entry` | Load `DS/ES/FS/GS/SS = 0x10`, `ESP = KERNEL_STACK_TOP (0x9F000)`, `EBP = 0`, `EBX = 0x0500`, `EAX = 0x10000`, `JMP EAX` |
| 10 | Kernel entry | `kernel_entry` | `CLI`, `CLD`, stack reset, then the init sequence below |

### 2.1 Kernel init order

`kernel_entry` runs a fixed sequence; a failure in the early phases leaves
interrupts disabled, so no half-initialised handler can fire.

```text
CLI, CLD, ESP=0x9F000, EBP=0
serial_init            COM1 115200 8N1, FIFO, polled TX
log_init               ring buffer + boot banner
gdt_init               reload kernel GDT (null / code 0x08 / data 0x10)
pmm_init               E820 -> physical page bitmap
heap_init              one free block over HEAP_START..HEAP_END
pic_init               remap 8259 to 0x20/0x28, mask all lines
idt_init               256 gates (exceptions 0..31, IRQs 32..47, default stub)
syscall_init           install INT 0x80 gate (DPL 3)
pit_init               8254 channel 0, 100 Hz
pic_unmask(0)          enable the timer IRQ line only
rtc_init               read CMOS time once
keyboard_init          clear state, unmask IRQ1
snn_init               zero state, generate weight matrix from fixed seed
theme_init             upload 256-entry palette to the DAC
vga_init               clear back buffer, present an empty frame
ui_init                reset frame-pacing state
STI                    interrupts are now legal
selftest_run           10 boot checks, results to log/HUD
main loop              ui_frame(); forever
```

Boot log lines (COM1 + ring buffer), in order:

```text
[INFO ] logger online: unified serial + ring buffer
[INFO ] ZenithOS v1.0 kernel entered (protected mode, flat segments)
[INFO ] arch: GDT reloaded, flat 4 GiB ring-0 segments
[INFO ] mm: physical page allocator armed from E820
[INFO ] mm: kernel heap free-list online
[INFO ] arch: IDT + PIC remapped to 0x20/0x28, exceptions gated
[INFO ] drivers: PIT 100 Hz, IRQ1 keyboard, CMOS RTC
[INFO ] ai: 24-neuron spiking network online
[INFO ] video: VGA mode 13h back-buffered 320x200x256
[INFO ] ui: neon neural core renderer initialised
[INFO ] kernel: interrupts enabled, perception loop running
[INFO ] selftest: starting 10 checks
...      (one line per check)
[INFO ] selftest: complete
[INFO ] ZenithOS v1.0 boot complete
```

---

## 3. Physical memory map

Everything below 1 MiB is statically assigned. The PMM only ever allocates
frames at or above 1 MiB; the kernel's own data structures live in the low
1 MiB window reserved by construction.

| Range | Size | Owner / use |
| --- | --- | --- |
| `0x000000–0x0003FF` | 1 KiB | Real-mode IVT (dead after boot) |
| `0x000400–0x0004FF` | 256 B | BIOS data area (dead after boot) |
| `0x000500–0x00050F` | 16 B | Boot-info block: `+0` magic `ZENO`, `+4` version 1, `+6` E820 count (u16), `+8` boot drive (u8) |
| `0x000510–0x0005CF` | 192 B | E820 entries, max 8 × 24 B |
| `0x0007C00–0x0007DFF` | 512 B | Boot sector while executing (dead afterward) |
| `0x0010000–0x001EFFF` | 60 KiB | Flat kernel image (`KERNEL_ORG`), 61,440-byte hard budget |
| `0x0020000–0x002F9FF` | 64,000 B | VGA back buffer (`VGA_BACKBUF`) |
| `0x0030000–0x0033FFF` | 16 KiB | PMM bitmap: 131,072 frames × 4 KiB = 512 MiB tracked |
| `0x0034000–0x006FFFF` | ~304 KiB | Kernel heap arena (`HEAP_START`..`HEAP_END`) |
| `0x0070000` | — | `AI_SCRATCH` base |
| `0x0074000` | — | `UI_SCRATCH` base |
| `0x009F000` | — | Kernel stack top (`KERNEL_STACK_TOP`, grows down) |
| `0x00A0000–0x00AF9FF` | 64,000 B | VGA mode 13h framebuffer (`VGA_FB`) |
| `0x00F0000+` | — | BIOS ROM / firmware MMIO (untouched) |

```text
 0x000000  +--------------------------+
           | IVT / BDA / bootinfo     |
 0x0007C00 | boot sector (transient)  |
 0x0010000 | kernel image (<= 60 KiB) |
 0x0020000 | back buffer (64,000 B)   |
 0x0030000 | PMM bitmap (16 KiB)      |
 0x0034000 | heap arena (~304 KiB)    |
 0x0070000 | AI scratch               |
 0x0074000 | UI scratch               |
 0x009F000 | stack top (grows down)   |
 0x00A0000 | VGA framebuffer          |
 0x00F0000 | BIOS ROM                 |
           +--------------------------+
```

The kernel is loaded at `0x10000` rather than `0x1000` for a concrete reason:
the boot sector is still executing at `0x7C00`, and a 60 KiB window starting at
`0x1000` would overwrite the running boot sector. `0x10000` puts the whole
window safely above the boot sector and below the back buffer.

---

## 4. Module interfaces

The table lists the public entry points and the data that crosses module
boundaries. Assembly has no headers; the contract *is* the register/`extern`
documentation at the top of each file. The build's `-DZENITH_OBJ_CHECK` pass
makes every `extern` explicit.

### 4.1 `src/arch/gdt.asm`

| Symbol | Contract |
| --- | --- |
| `gdt_init` | Loads the kernel GDT and reloads `CS` via far jump; sets `DS/ES/FS/GS/SS = 0x10` |
| `gdt_start` | Base of the 3-descriptor table; used by the GDT self-test (`sgdt` base and limit 23) |

Descriptors: null (`0x00`), flat code (`0x08`, present, ring 0, `0xCF`
granularity/limit), flat data (`0x10`). No TSS — there is no ring 3.

### 4.2 `src/arch/idt.asm`

| Symbol | Contract |
| --- | --- |
| `idt_init` | Fills 256 gates with `isr_unused`, then installs 48 stubs from `isr_stub_table`, then `LIDT` |
| `idt_set_gate` | `EAX=vector`, `EBX=handler`, `ECX=attributes` — used by the syscall layer to install vector `0x80` with `0xEE00` (present, DPL 3, 32-bit interrupt gate) |
| `idt_table` | 256 × 8-byte gates; IDT limit is 2047 |

### 4.3 `src/arch/isr.asm`

| Symbol | Contract |
| --- | --- |
| `isr_0..isr_31` | Exception stubs; push synthetic error code where the CPU does not, then vector number, then join `isr_common` |
| `irq_0..irq_15` | IRQ stubs for vectors 32..47 |
| `isr_stub_table` | 48 dwords consumed by `idt_init` |
| `irq_handler_table` | 16 dwords; entry 0 = `pit_irq_handler`, entry 1 = `kbd_irq_handler`, others = `irq_default_handler` |

`isr_common` builds a uniform trap frame (`PUSHA`, then `DS/ES/FS/GS`), loads
kernel data selectors, and dispatches:

- vector `< 32` → `exception_panic(vector, error, EIP)` (never returns);
- vector `32..47` → `irq_dispatch` → handler → `pic_send_eoi(irq)`;
- any other vector → return quietly.

### 4.4 `src/arch/pic.asm`

`pic_init` remaps the pair to `0x20`/`0x28` and masks everything.
`pic_unmask(irq)` / `pic_mask(irq)` handle the cascade line automatically;
`pic_send_eoi(irq)` acknowledges slave then master; `pic_mask_all` re-masks.

### 4.5 `src/arch/syscall.asm`

`syscall_init` installs the DPL-3 `INT 0x80` gate; `syscall_entry` preserves
registers, dispatches on `EAX`, stores the return value, and `IRET`s. See
[§15](#15-system-call-abi).

### 4.6 `src/drivers/pit.asm`

| Symbol | Contract |
| --- | --- |
| `pit_init` | Channel 0, mode 3, divisor `1193182/100 = 11932`; zeroes `pit_ticks` |
| `pit_irq_handler` | IRQ0: `inc dword [pit_ticks]`; minimal by design |
| `pit_uptime_ms` | `pit_ticks * 10` |
| `pit_ticks` | Monotonic 32-bit tick counter at 100 Hz |

### 4.7 `src/drivers/serial.asm`

`serial_init` programs COM1 to 115,200 8N1 with FIFO enabled, interrupts
disabled, DTR/RTS asserted. `serial_putc` polls the transmitter-empty bit with
a bounded spin (`0x10000` iterations) so a missing UART cannot wedge the
kernel. `serial_puts` walks a NUL-terminated string.

### 4.8 `src/drivers/keyboard.asm`

| Symbol | Contract |
| --- | --- |
| `kbd_init` | Clears state and unmasks IRQ1 |
| `kbd_irq_handler` | Reads port `0x60`, tracks the `0xE0` prefix, maps scancode set 1, sets/clears the held bit, pushes make events into the ring |
| `kbd_poll` | `EAX = 0x100 | logical key`, or `0` when empty |
| `kbd_held_mask` | Bit `N` set while logical key `N` is held (see `KEY_*` in `config.inc`) |
| `kbd_scan_count` | Diagnostics counter |

Ring size `KBD_BUF_SIZE = 64` (power of two); overwrite-oldest on overflow.

### 4.9 `src/drivers/vga.asm`

All drawing targets the back buffer at `0x20000`; only `vga_present` touches
the aperture. Primitives: `vga_clear`, `vga_clear_dither`, `vga_putpixel`,
`vga_hline`, `vga_vline`, `vga_fill_rect`, `vga_line` (Bresenham, per-pixel
clipped), `vga_draw_char`, `vga_draw_char_big` (2×), `vga_draw_text`,
`vga_draw_text_big`, `vga_set_palette` (RGB 0..63 triples), `vga_present`
(`rep movsd` of 64,000 bytes), `vga_wait_vsync` (bounded poll of port `0x3DA`
bit 3).

Text is drawn from the 5×7 font: 5 column bytes per glyph, bit 0 = top row,
advance 6 px (12 px for 2×).

### 4.10 `src/drivers/rtc.asm`

`rtc_init` reads CMOS status B and the time once. `rtc_read_time` waits
(bounded) for the update-in-progress bit to clear, latches second/minute/hour/
day/month/year, converts packed BCD unless status B says binary, and
normalises 12-hour PM/AM to 24-hour. The kernel never writes the RTC.

### 4.11 `src/mm/pmm.asm`

| Symbol | Contract |
| --- | --- |
| `pmm_init` | Mark all frames used; free E820 type-1 ranges ≥ 1 MiB (capped at 512 MiB); fallback = 32 MiB if the map is missing |
| `pmm_alloc_page` | Returns a 4 KiB-aligned physical address, or `0`; scans from a hint, wraps once |
| `pmm_free_page` | Frees a page; ignores double-free and out-of-range addresses |
| `pmm_free_pages`, `pmm_total_pages`, `pmm_usable_kb`, `pmm_free_kb` | Statistics |

### 4.12 `src/mm/heap.asm`

First-fit free list over `HEAP_START..HEAP_END`, 8-byte aligned; header is
`{u32 size|alloc_bit, u32 next}`; splitting when the remainder ≥ 16 bytes;
forward and backward coalescing on free. API: `heap_init`, `kmalloc`,
`kfree`, `heap_free_bytes`, `heap_used_bytes`.

### 4.13 `src/ai/snn.asm`

`snn_init`, `snn_tick`, `snn_stimulate(index)`; state `snn_activation[24]`,
`snn_spiked[24]`, `snn_weights[576]`, `snn_spike_count`,
`snn_spikes_this_tick`, `snn_energy`. See [§10](#10-the-spiking-neural-network).

### 4.14 `src/ui/theme.asm`

`theme_init` uploads the 256-entry generated palette (768 bytes of RGB 0..63)
to the DAC via `vga_set_palette`. Slots 0..15 are stable so the panic screen is
readable regardless of init order; slots 16..191 are neon ramps
(`RAMP_NODE`, `RAMP_EDGE`, `RAMP_GRID`, `RAMP_UI`, `RAMP_LOG`, `RAMP_GREEN`,
`RAMP_AMBER`, `RAMP_RED`, `RAMP_BG`).

### 4.15 `src/lib/*` and `src/ui/*`

| Module | Provides |
| --- | --- |
| `lib/string.asm` | `str_len`, `mem_copy`, `mem_set`, `mem_zero`, `u32_to_dec`, `u32_to_dec_pad`, `u32_to_hex` |
| `lib/fixed.asm` | `fsin`, `fcos` (Q8.8 via generated table), `fmul_q8`, `sat_u8`, `abs_i32`, `clamp_i32` |
| `lib/log.asm` | `log_init`, `log_info`, `log_warn`, `log_error`, `log_str`; ring state (`log_*`) |
| `lib/panic.asm` | `exception_panic`, exception name table |
| `lib/selftest.asm` | `selftest_run`, `selftest_passed`, `selftest_total` |
| `ui/font.asm` | `font_5x7`: 95 glyphs × 5 bytes, ASCII 32..126 |
| `ui/neural.asm` | `ui_init`, `ui_frame` and the desktop renderer |

---

## 5. Interrupt path

### 5.1 Trap frame layout

`isr_common` runs with the stack looking like this (low addresses at the top):

```text
  [ GS ][ FS ][ ES ][ DS ][ EDI..EAX (PUSHA) ][ vector ][ error ][ EIP ][ CS ][ EFLAGS ]
       ^ pushed by isr_common                     ^ pushed by the stub / CPU
```

- `[esp + 48]` = vector
- `[esp + 52]` = error code
- `[esp + 56]` = faulting `EIP`

The exception path passes `(vector, error, EIP)` to `exception_panic`; the IRQ
path calls the registered handler and then sends EOI. Unused gates point at
`isr_unused`, which pushes vector `0xFFFF` and returns.

### 5.2 Flow

```text
  hardware IRQ (PIC) ──> IDT gate (DPL0 interrupt gate) ──> irq_N stub
                                                      │
                                                      v
                                              isr_common (PUSHA, segments, CLD)
                                                      │
                              vector 32..47 ──> irq_dispatch
                                                      │  handler = table[vector-32]
                                                      │  call handler
                                                      │  pic_send_eoi(irq)
                                                      v
                                              IRETD (restores frame)

  CPU exception ──> IDT gate ──> isr_N stub ──> isr_common ──> exception_panic
                                                              (never returns)
```

Because the dispatcher sends the EOI, drivers never touch the PIC. Because the
kernel runs one main loop with interrupts enabled, timer and keyboard events
preempt rendering — the desktop is interrupt-driven, not polled.

---

## 6. PIC and PIT

### 6.1 8259 remap

The BIOS leaves IRQ0..7 at vectors `0x08..0x0F`, colliding with CPU
exceptions. `pic_init` issues the standard ICW1–ICW4 sequence:

| ICW | Master (`0x20`/`0x21`) | Slave (`0xA0`/`0xA1`) |
| --- | --- | --- |
| ICW1 | `0x11` — edge, cascade, ICW4 | `0x11` |
| ICW2 | `0x20` — vector base | `0x28` |
| ICW3 | `0x04` — slave on IRQ2 | `0x02` — cascade identity |
| ICW4 | `0x01` — 8086 mode | `0x01` |
| masks | `0xFF` | `0xFF` |

Drivers unmask only the lines they own: IRQ0 (PIT) and IRQ1 (keyboard). IRQ2 is
unmasked automatically when a slave line is used.

### 6.2 8254 PIT

- Channel 0, command `0x36` (lobyte/hibyte, mode 3, binary).
- Divisor `1193182 / 100 = 11932` → 100.00 Hz (programmed value truncates, so
  the actual rate is 100.0027 Hz; the kernel reasons in ticks, never in
  seconds).
- `pit_ticks` is the only clock. Uptime = `ticks × 10 ms`; the HUD also derives
  `T+MM:SS` from it.

---

## 7. Keyboard

Scancode set 1 (XT-compatible). The handler:

1. reads port `0x60`;
2. if the previous byte was `0xE0`, clears the prefix and maps the extended
   code (arrow keys);
3. otherwise masks bit 7 to get the make code and maps it (`W=0x11`,
   `A=0x1E`, `S=0x1F`, `D=0x20`, `Q=0x10`, `E=0x12`, `R=0x13`,
   `Space=0x39`, `Esc=0x01`, `Enter=0x1C`, `C=0x2E`);
4. sets or clears `kbd_held_mask` bit for the logical key;
5. on a make code, pushes the key into the 64-entry ring.

`ui_input` drains the ring each frame. `W/A/S/D` stimulate SNN neurons 0–3,
`Q`/`E` stimulate 4/5, `Space` stimulates 7 (each `+96`, saturating), so
keyboard input genuinely enters the network. Held keys are also available to
the camera through `kbd_held_mask`.

---

## 8. Physical memory allocator (PMM)

- One bit per 4 KiB frame, `PMM_BITMAP_ADDR = 0x30000`, 16 KiB →
  `PMM_MAX_PAGES = 131,072` frames → **512 MiB** tracked.
- `pmm_init` marks **all** frames used, then parses the E820 map captured at
  `0x0510`:
  - only type `1` (usable) entries are considered;
  - regions with a non-zero high dword (≥ 4 GiB) are skipped;
  - lengths are capped at the bitmap's 512 MiB reach;
  - everything below 1 MiB is force-reserved (`LOW_PAGE = 256`);
  - each surviving range is rounded inward to page boundaries and its bits
    cleared.
- If the boot magic is missing or the entry count is zero, a conservative
  32 MiB fallback is freed.
- `pmm_alloc_page` scans from `pmm_hint`, wraps to `LOW_PAGE` once, and returns
  a `page << 12` address or `0` on exhaustion.
- `pmm_free_page` ignores addresses below 1 MiB, out of range, or already free
  (double-free is a no-op).

The allocator is **not used by the desktop** in v1.0 (all UI state is static or
heap-allocated); it exists, is self-tested, and is the substrate for future
drivers and userspace.

---

## 9. Kernel heap

- Arena: `HEAP_START (0x34000)` .. `HEAP_END (0x70000)`, ≈ 304 KiB.
- `heap_init` creates one free block covering the arena minus one header.
- Block layout:

```text
  +------------------------+  <- block address (returned pointer - 8)
  | size | allocated bit   |   u32, low bit = allocated, size multiple of 8
  +------------------------+
  | next free/alloc block  |   u32 absolute address (0 = end)
  +------------------------+  <- pointer returned by kmalloc
  | payload ...            |
  +------------------------+
```

- `kmalloc`: rounds up to 8 bytes, walks the free list first-fit, splits when
  the remainder can hold a header plus ≥ 8 bytes of payload (`MIN_SPLIT = 16`).
- `kfree`: marks the block free, coalesces forward and backward when the
  neighbour is physically adjacent and free; double-free is detected (allocated
  bit already clear) and ignored.
- `heap_free_bytes` walks the list; `heap_used_bytes` returns the running
  counter. Both are displayed by the HUD/self-test.

The heap is deliberately separate from the PMM: the PMM owns physical frames
above 1 MiB for future use; the heap gives the kernel a simple, bounded arena
today.

---

## 10. The spiking neural network

`src/ai/snn.asm` implements a deterministic, integer-only leaky
integrate-and-fire network. It is the AI-native heart of the desktop: the
renderer reads its state every frame.

### 10.1 State

| Symbol | Type | Meaning |
| --- | --- | --- |
| `snn_activation[24]` | u8 | activation (membrane potential surrogate), 0..255 |
| `snn_spiked[24]` | u8 | spike flags for the current tick |
| `snn_weights[576]` | u8 | excitatory weight matrix `W[pre][post]`, 0..40 |
| `snn_random` | u32 | xorshift32 state, seeded `0x13579BDF` |
| `snn_spike_count` | u32 | total spikes since boot |
| `snn_spikes_this_tick` | u32 | spikes on the last tick |
| `snn_energy` | u32 | `sum(activation)`, 0..6120 |

### 10.2 Weight generation

`snn_init` fills `W` with a fixed xorshift32 sequence. For each of the 576
entries:

```text
r = xorshift32()
weight = min(r & 0x3F, 40)
if (r & 0xC0) == 0: weight = 0     ; ~25% of synapses absent
W[i] = weight
```

The matrix is therefore sparse (≈ 75% non-zero) and fully deterministic.

### 10.3 Per-tick update

`snn_tick` runs once per PIT tick (the UI may run up to 4 catch-up ticks if it
falls behind):

```text
1. clear spiked[]
2. for each neuron: a -= a >> 3          (12.5% leak per tick)
3. ambient stimulus:
     r = xorshift32()
     if (r & 7) == 0:
         target = r mod 24
         a[target] = min(a[target] + 72, 255)
4. for i in 0..23:
     if a[i] >= 192:
         a[i] = 0
         spiked[i] = 1
         spike_count++; spikes_this_tick++
         for j in 0..23:
             if W[i][j] != 0:
                 a[j] = min(a[j] + W[i][j], 255)
5. energy = sum(a)
```

`snnt_stimulate(index)` adds 96 (saturating) and is called by the UI for
`W/A/S/D/Q/E/Space` key events. All arithmetic is unsigned 8-bit with
saturating clamps; the energy sum is 32-bit. There is no floating point, no
allocation, and no wall-clock dependency.

### 10.4 Why these choices

- **One byte per neuron** keeps the whole model in 624 bytes of state and makes
  HUD rendering a shift-and-scale operation.
- **Saturating adds** mean no input sequence can overflow state or fault the
  kernel.
- **Fixed seed** means the desktop is reproducible: the same build produces the
  same spike trains for the same input timeline.
- **No inhibition/refractory** keeps the dynamics lively at 100 Hz; the leak
  provides the only decay.

---

## 11. Rendering pipeline

The desktop is a 320×200 indexed-colour scene rendered entirely by the kernel
into the back buffer, then blitted to the VGA aperture. Rendering is paced by
the 100 Hz timer: `ui_frame` runs at most once per tick.

### 11.1 Frame loop

```text
ui_frame:
    ui_input        drain keyboard ring -> camera + SNN stimulus
    ui_ai_step      advance SNN once per elapsed tick (max 4 catch-up ticks)
    ui_draw         compose the scene into the back buffer
    vga_present     rep movsd 64,000 bytes -> 0xA0000
    ui_wait_tick    spin until pit_ticks advances (pacing)
```

Because the loop waits for the next heartbeat after presenting, the desktop is
locked to the timer rather than running free; if a frame overruns, the wait
returns immediately and the SNN catches up by at most 4 ticks.

### 11.2 Camera

| Input | Effect | Speed / limits |
| --- | --- | --- |
| `W` / `S` | dolly in / out | `ui_zoom ∓ 3` per frame; clamped 130..560; default 240 |
| `A` / `D` | yaw | `ui_yaw ∓ 96` per frame; ×3 with `Space` |
| `Q` / `E` | pitch | `ui_pitch ± 64` per frame; ×2 with `Space`; clamped ±6000 |
| `R` | reset | yaw = 0, pitch = 0, zoom = 240, auto = on |
| `C` | toggle cinematic auto-rotation | — |
| `Space` | boost | multiplies yaw/pitch sensitivity for held keys |
| `↑` / `↓` | dolly in / out (mirror of W/S) | same as W/S |
| `←` / `→` | yaw (mirror of A/D) | same as A/D |

Camera angles are 16-bit binary radians (`0x0000..0xFFFF` = one turn); the top
8 bits index the generated 256-entry Q8.8 sine/cosine table. All rotation is
integer multiplication plus arithmetic shift, never floating point.

With cinematic mode enabled (default) and no movement key held, `ui_yaw += 10`
per frame. Touching any movement key clears `ui_auto`; `C` toggles it back on
and `R` restores it. The pitch clamp of ±6000 units is roughly ±33°.

### 11.3 Scene composition

Per frame, in this exact order:

1. **Projection** (`ui_project`): for each of the 24 model nodes, rotate by
   yaw around Y, then pitch around X, add the Q8.8 zoom to the resulting
   depth, cull anything with depth ≤ 10 px, and project:
   `sx = 160 + (x*260)/depth`, `sy = 100 + (y*260)/depth` (`UI_FOV = 260`).
2. **Depth sort** (`ui_sort_nodes`): insertion sort of the 24 node indices by
   `ui_sz`, far to near (painter's algorithm).
3. **Background** (`ui_draw_background`): `vga_clear_dither` alternates
   `RAMP_BG+1` and `RAMP_BG+3` per row for a CRT texture.
4. **Starfield** (`ui_draw_stars`): 72 stars generated once by xorshift32 with
   seed `0xC0FFEE11`; each frame's colour advances through
   `RAMP_UI+1 .. RAMP_UI+8` by `(phase + tick/8) & 7`.
5. **Floor grid** (`ui_draw_grid`): horizontal lines at `y = 126, 134, … < 168`;
   radial lines from the vanishing point `(160,112)` to the bottom edge at
   `(k*40, 172)` for `k*40 < 320`; a horizon glow line at `y = 112`.
6. **Edges** (`ui_draw_edges`): for each of the 40 edges with both endpoints
   on-screen, compute an average-depth shade (`0..31`, near = bright), map it
   into the edge ramp (`RAMP_EDGE+1 .. +31`), then draw a soft glow line
   offset one pixel down in a darker ramp entry and a bright core line.
7. **Packets** (`ui_draw_packets`): 12 bright 2×2 dots; edge index
   `(tick/32 + k*7) mod 40`, interpolation `t = (tick*3 + k*29) & 255`,
   colour pulsed through `RAMP_NODE+27..30`.
8. **Neurons** (`ui_draw_nodes`): for each node in the sorted order, radius
   `= 4 + activation/32 + shade/4` (clamped to 12). Draw an outer halo, a
   spike ring if the neuron fired this tick, an inner core, and a white-hot
   2 px centre when activation ≥ 160. Circles use squared-radius integer
   rasterisation (`ui_draw_glow`, clipped to the framebuffer).
9. **HUD** (`ui_draw_hud`): see below.
10. **Present** (`vga_present`).

### 11.4 Geometry and fixed point

Model coordinates are Q8.8 (`1 px = 256`). The 24 nodes are:

| Indices | Position |
| --- | --- |
| 0..7 | ring A: radius 88 px, `y = −42 px`, angle `i × 32` binary radians |
| 8..15 | ring B: radius 88 px, `y = +42 px`, angle `i × 32 + 16` |
| 16..19 | axis nodes: `(±100, 0, 0)`, `(0, 0, ±100)` |
| 20..23 | core tetrahedron at ±35 px on each axis |

The 40 edges are: for every `i` in 0..7, four connections (A-ring next,
B-ring next, A_i–B_i, A_i–B_(i+1)) = 32; plus 4 axis→core links and 4
axis→ring links = 40.

### 11.5 HUD layout

| Region | Content |
| --- | --- |
| Top bar (0..21 px) | background fill, two cyan lines, `ZENITHOS` (2×) at (8,3), `// NEURAL CORE` at (112,9) |
| Top-right | `T+MM:SS` uptime at y=4, `FPS n  AI n/s` at y=12, right-aligned to x=313 |
| Bottom bar (178..199 px) | background fill, controls line at (6,181): `W/S ZOOM  A/D ROTATE  Q/E PITCH  R RESET  C AUTO`; telemetry at (6,190): `PMM <MiB> MB FREE  HEAP <KiB> KB  SELFTEST p/t`; CMOS `HH:MM:SS` right-aligned to x=314 |
| Log ticker (158..172 px) | newest unified-log line at (6,161) in the violet log ramp; build tag `V1.0 // 24N 40E` on the right |
| Corners | cyan/hot-pink accent brackets at (2,24) and (308,24) |

FPS is computed once per second (frames counted over 100 ticks); the spike rate
is the per-second delta of `snn_spike_count`; the RTC is re-read once per
second.

### 11.6 Why double buffering

Drawing directly to `0xA0000` would tear: the VGA beam scans while the CPU
plots. Instead every primitive writes to `0x20000`, and a single contiguous
`rep movsd` copies the finished frame. That also makes the frame
deterministic: given the same state, the same 64,000 bytes are produced, which
is what the screenshot harness hashes.

---

## 12. Palette and DAC

- VGA mode 13h is 8-bit indexed. The kernel programs all 256 DAC entries via
  ports `0x3C8` (index) and `0x3C9` (data, 6-bit channels).
- The palette is **generated at build time** by `build.js` from ramp
  declarations into `build/generated/palette.inc` (768 bytes), so the colour
  language is reproducible and reviewed as source.
- Fixed slots 0..15 are reserved (black, white, cyan, magenta, red, green,
  amber, background, dim, blue, violet, hot pink, bright red, teal, steel,
  white2) so the panic screen reads correctly whether it fires before or after
  `theme_init`.
- Ramps: node (16..47), edge (48..63), floor grid (80..95), UI cyan (96..111),
  log violet (112..127), green/amber/red status ramps, near-black background
  shades (176..191).
- Scanline texture is produced by `vga_clear_dither` alternating two
  background shades per row — no extra framebuffer pass.

---

## 13. Unified logging

One API, two sinks, one truth:

```text
  log_info/log_warn/log_error  ──> last-message state (HUD)
          │                         │
          ├──> COM1 (polled TX)     └──> 32-entry ring {ptr, tick, level}
          │
          └──> serial capture in CI
```

- Levels: `0` info, `1` warn, `2` error. Prefixes `[INFO ]`, `[WARN ]`,
  `[ERROR]` are written to serial.
- Every entry records the message pointer and the tick at which it was logged.
- The ring wraps at 32 entries; `log_count` counts entries (capped) and
  `log_error_count` counts errors. `log_last_msg` / `log_last_level` feed the
  HUD.
- The self-tests report through the same logger, so the serial transcript and
  the screen cannot disagree.

The logger is safe to call before the heap or PMM exist (fixed buffers only)
and is used from `kernel_entry` before `STI`.

---

## 14. Panic path

Any CPU exception (vectors 0..31) reaches `exception_panic(vector, error,
EIP)`. The path is deliberately minimal: no heap, no allocation, no
interrupts, only already-initialised VGA primitives and polled serial.

1. `CLI`; capture vector, error code, `EIP`, `CR0`, `CR2`, `CR3`.
2. Write the record to COM1 **first**:
   `PANIC vector=0x........ error=0x........ eip=0x........`.
3. Paint the panic screen in mode 13h:
   - red bars top and bottom,
   - `KERNEL PANIC` (2× font), subtitle,
   - `VECTOR` hex, `NAME` (vectors 0..31 only, table in `panic.asm`),
   - `ERROR` hex, `EIP` hex, `CR2` hex, `CR0` hex,
   - `SYSTEM HALTED - RESET REQUIRED`, `full record was written to COM1`.
4. Present the frame, then `CLI; HLT` forever.

`CR3` is captured in memory but not shown on the small screen (there is no
paging, so it is informational). The panic names include `DIVIDE ERROR`,
`INVALID OPCODE`, `DOUBLE FAULT`, `GENERAL PROTECTION`, `PAGE FAULT`, and the
rest of the architectural vectors.

---

## 15. System-call ABI

A native (not POSIX) `INT 0x80` ABI exists as groundwork for a future
userspace. The gate is installed with DPL 3 so a ring-3 caller could use the
same vector unchanged; today the self-test calls it from ring 0.

| `EAX` | Name | Arguments | Returns |
| --- | --- | --- | --- |
| `0` | `SYS_TICKS` | — | `pit_ticks` |
| `1` | `SYS_WRITE` | `EBX`=string, `ECX`=length | length; bytes go to COM1 |
| `2` | `SYS_SNN_ENERGY` | — | `snn_energy` |
| `3` | `SYS_HEAP_USED` | — | `heap_used_bytes()` |
| `4` | `SYS_PMM_FREE_KB` | — | `pmm_free_kb()` |
| other | — | — | `0xFFFFFFFF` (−1) |

`syscall_entry` pushes registers (`PUSHA`), calls `syscall_dispatch`, stores the
result, pops, and `IRET`s. There is no user/kernel copy validation because
there is no user address space; the ABI is a mechanism proof, not a security
boundary.

---

## 16. Boot self-tests

`selftest_run` executes 10 checks after `STI` and before the main loop. Each
result is logged (`selftest: <name>` on success, `[ERROR]` on failure) and
counted into `selftest_passed/selftest_total`; the HUD shows `SELFTEST n/10`.

| # | Check | Assertion |
| --- | --- | --- |
| 1 | Boot-info hand-off | magic `0x4F4E455A` and version 1 at `0x0500` |
| 2 | GDT | `sgdt` base = `gdt_start`, limit = 23 |
| 3 | IDT | `sidt` base = `idt_table`, limit = 2047 |
| 4 | PMM | two allocations are non-zero, distinct, and freeing both restores the free count |
| 5 | Heap | allocate 64 bytes, aligned inside the arena, write/read canaries, free, free-bytes restored |
| 6 | SNN | 48 ticks produce at least one spike and bounded energy (`≤ 24×255`) |
| 7 | RTC | second ≤ 59, minute ≤ 59, hour ≤ 23, month 1..12 |
| 8 | PIT delivery | tick counter advances during a ~300,000-iteration spin |
| 9 | VGA back buffer | read, write, restore a byte at `0x20000` |
| 10 | `INT 0x80` | `SYS_TICKS` returns a non-zero tick value |

The tests are intentionally shallow: they catch broken invariants early, in
graphics mode, without a debugger. They are not a security test suite.

---

## 17. Build pipeline

`node build.js` — no npm dependencies. Pipeline:

```text
1. find NASM            env NASM, ./nasm.exe, ./nasm, ./bin, Program Files, PATH
2. parse config.inc     KERNEL_SECTORS (120), KERNEL_ORG (0x10000)
3. generate trig.inc    256 Q8.8 sin/cos entries (512 B)
4. generate palette.inc 256 RGB 0..63 triples (768 B)
5. module gate          every src/**/*.asm except kernel.asm:
                        nasm -f obj -DZENITH_OBJ_CHECK -I src -I build/generated
6. assemble boot.bin    nasm -f bin -I src
7. assemble kernel.bin  nasm -f bin -I src -I build/generated
8. validate             boot 512 B + 0xAA55; kernel <= 61,440 B
9. pack image           boot @ 0, kernel @ 512, zero fill to 1,474,560
10. read back           boot bytes, kernel bytes, zero tail
11. report              build/report.json (modules, hashes, checks, timings)
12. print SHA-256       of build/zenithos.img
```

CLI flags: `--no-check` (skip the module gate), `--quiet`, `--verbose`. The
image is written to both `build/zenithos.img` and `zenithos.img`; build products
are gitignored.

`build/report.json` is the machine-readable provenance record: NASM version,
module object sizes, boot/kernel/image hashes, image geometry, boolean checks,
generated-asset hashes, and durations. CI archives it with the image.

**Verified v1.0.0 release artifact:**

| Artifact | Size | SHA-256 |
| --- | --- | --- |
| `build/boot.bin` | 512 B | `36e551a14909f70b51336596e5d0dacb0bf02a6b3651575f15110abc32f439a8` |
| `build/kernel.bin` | 19,376 / 61,440 B (31.5%) | `88926f1c573f234088e82687e4d9eed8f78a6a8760961268e319876ddb0b4efe` |
| `build/zenithos.img` | 1,474,560 B | `a5a9d9f26af8ae82429c593539f9d14059d60013dbbb3b1345758dbaf3513d13` |

---

## 18. Headless verification harness

`tools/emulate.mjs` boots an image under v86 (the WebAssembly x86 emulator)
without a browser:

- loads BIOS images from `tools/vendor/` (auto-downloaded and size-checked if
  missing), the floppy image, 64 MiB RAM, 8 MiB VGA;
- installs a minimal `ImageData` shim so v86 allocates its RGBA buffer in Node;
- waits a configurable time (default 6 s) for the guest to enter a graphical
  mode;
- composes the visible 320×200 frame from the VGA layers, writes PNGs with a
  dependency-free encoder (`tools/lib/png.mjs`), optionally scaled 3×;
- can inject keys (`--keys "w,w,d,r"`, PS/2 set-1 scancodes through the
  emulated 8042), capture before/after frames, and diff them;
- can capture COM1 output (`--serial`), and records CPU exception vectors;
- fails if the frame is blank/uniform/non-graphical (exit 3), if the emulator
  faults (exit 2), or on usage/IO errors (exit 4); PASS is exit 0;
- writes crash diagnostics (`<out>-crash.txt`, `<out>-crash.png`) on failure.

The PASS gate requires ≥ 1% non-black pixels, ≥ 16 distinct colours, and a
non-uniform frame. That is enough to distinguish a rendered desktop from a
black screen without pinning every pixel; exact golden-frame comparison is
v1.1 work (see [`TESTING.md`](TESTING.md)).

Measured v1.0 gate result: 320×200, **100.00% non-black pixels, 61 distinct
colours**, a **~15–17% frame change** after injected WASD input, and
**~30–34 FPS** under the emulator. The FPS figure is an emulator measurement,
not a hardware benchmark: the kernel paces frames to the 100 Hz PIT and is
designed for 100 fps on real hardware.

---

## 19. Determinism and invariants

The system is built to be reproducible and to fail loudly:

1. **Deterministic build.** NASM assembles static sources; the only generated
   inputs are pure functions of constants (trig, palette). The image is
   zero-padded and the SHA-256 is printed; two builds of the same source
   produce the same hash.
2. **Deterministic desktop.** SNN state is integer and seeded with a constant;
   the renderer is a pure function of SNN state, camera state, tick count, and
   static geometry. No `RDTSC`, no RTC in the render path.
3. **Fixed memory contract.** `config.inc` is shared by loader and kernel;
   `build.js` checks the kernel against the load window; the PMM never hands
   out the same frame twice (bit set on alloc, cleared on free).
4. **Single CPU, no preemption beyond IRQs.** The main loop and interrupt
   handlers only share data through word-sized counters and ring indices;
   there are no locks because there is only one core.
5. **Interrupts disabled until the IDT is installed.** The boot sector never
   executes `STI`; the kernel enables interrupts only after all handlers,
   drivers, and the syscall gate are ready.
6. **No heap in the panic path.** `exception_panic` cannot allocate, so a heap
   corruption cannot prevent the fault report.

Known non-invariants (documented limits): no memory isolation, no privilege
separation, no scheduler, no paging, no storage, no networking, and no
multi-user model. See [`SECURITY.md`](SECURITY.md).

---

## 20. Source index

| Path | Lines (v1.0) | Purpose |
| --- | --- | --- |
| `boot.asm` | 275 | 512-byte real-mode bootstrap |
| `src/kernel.asm` | 134 | composition root, entry vector, main loop, boot log |
| `src/include/config.inc` | 104 | ABI constants and physical memory map |
| `src/include/macros.inc` | 36 | I/O, delay, assertion macros |
| `src/arch/gdt.asm` | 60 | flat GDT |
| `src/arch/idt.asm` | 92 | 256-gate IDT + `idt_set_gate` |
| `src/arch/isr.asm` | 173 | exception/IRQ stubs, dispatcher |
| `src/arch/pic.asm` | 132 | 8259 remap/mask/EOI |
| `src/arch/syscall.asm` | 102 | native `INT 0x80` ABI |
| `src/drivers/pit.asm` | 46 | 8254 @ 100 Hz |
| `src/drivers/serial.asm` | 82 | COM1 polled output |
| `src/drivers/keyboard.asm` | 219 | IRQ1, scancode set 1, ring |
| `src/drivers/vga.asm` | 543 | mode 13h primitives, palette, text |
| `src/drivers/rtc.asm` | 137 | CMOS clock |
| `src/mm/pmm.asm` | 262 | E820 physical frame bitmap |
| `src/mm/heap.asm` | 233 | first-fit kernel heap |
| `src/ai/snn.asm` | 211 | 24-neuron spiking network |
| `src/ui/font.asm` | 118 | 5×7 bitmap font |
| `src/ui/theme.asm` | 29 | generated palette upload |
| `src/ui/neural.asm` | 1499 | 3D neural-mesh desktop renderer |
| `src/lib/fixed.asm` | 81 | Q8.8 trig and clamps |
| `src/lib/string.asm` | 167 | string/format helpers |
| `src/lib/log.asm` | 122 | unified logger |
| `src/lib/panic.asm` | 271 | panic screen + serial record |
| `src/lib/selftest.asm` | 278 | 10 boot self-tests |
| `build.js` | 447 | build, generation, packing, verification |
| `tools/emulate.mjs` | 865 | headless v86 harness |

Line counts are from the v1.0 source and may drift with refactors; the module
list and ABI are the durable contract.

---

*Architecture document for ZenithOS v1.0. If this document and the source
disagree, the source wins — and the document is a bug.*
