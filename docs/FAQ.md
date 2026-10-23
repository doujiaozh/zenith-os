# ZenithOS FAQ

Twenty-four questions people actually ask, answered without marketing.
Everything here reflects v1.0 unless it says "planned".

---

### 1. Is ZenithOS a real operating system, or a demo?

It is a real, self-contained kernel: a 512-byte boot sector loads a 32-bit
protected-mode kernel that installs its own GDT, IDT, PIC, PIT, keyboard
driver, memory allocators, logger, and a graphical desktop. It does not depend
on DOS, Linux, Windows, a bootloader like GRUB, or a host OS. It is also a
small, early system — "real" does not mean "complete". Read
[`ROADMAP.md`](../ROADMAP.md) for what is missing.

### 2. Does it run Linux/Windows binaries or DOOM?

No. There is no userspace, no ELF loader, no system-call ABI, no filesystem, and
no C library in v1.0. The only program it runs is its own kernel. A POSIX
subsystem is planned for v3.0; see
[`POSIX-COMPATIBILITY.md`](POSIX-COMPATIBILITY.md).

### 3. Why 32-bit x86? Why a floppy image?

Because the target is the smallest possible distance between power-on and
pixels. 32-bit protected mode is the simplest flat-memory environment to
bootstrap; a raw 1.44 MB floppy image is the simplest boot medium (no
partition table, no filesystem, no bootloader chain). The constraints force
good engineering: every byte and every module has to justify itself. UEFI,
x86-64, and other architectures are possible later (see the roadmap) but are
not v1.0 goals.

### 4. How big is it?

The boot sector is exactly **512 bytes**. The kernel has a hard budget of
**120 sectors = 61,440 bytes (60 KiB)**, loaded from LBA 1 by CHS to physical
`0x10000`. The image is exactly **1,474,560 bytes** (1.44 MB), zero-padded.
The build prints the kernel's fill percentage and fails if it exceeds the
window. Check the actual number with `node build.js`.

### 5. In what sense is it "AI-native"? Is there an LLM?

There is a deterministic **24-neuron spiking neural network** in the kernel
(`src/ai/snn.asm`), driven by the 100 Hz timer and keyboard stimulus. Its
activation pattern drives the glow, pulses, and HUD of the desktop. There is
**no large language model**, no training, and no hallucination surface. "AI-
native" means the intelligence is a kernel subsystem that the whole UX is built
around, not an app bolted on top. Details:
[`AI-NATIVE.md`](AI-NATIVE.md).

### 6. Does it phone home / need the cloud / collect telemetry?

No. v1.0 has no network stack and no persistent storage. The SNN is local and
deterministic. There is no telemetry channel of any kind. Privacy is a
consequence of the architecture, not a policy promise.

### 7. What hardware does it support?

An IBM-PC-compatible machine with BIOS (legacy, not UEFI), a 1.44 MB floppy
controller, a VGA-compatible adapter, an 8042-style keyboard controller,
an 8254 PIT, and an 8259 PIC. That set is emulated faithfully by QEMU's
`qemu-system-i386` and by v86. Anything else — USB keyboards, NVMe, modern
GPUs — is not supported in v1.0.

### 8. Can I boot it on real hardware?

Often, yes, if the machine still has a BIOS, a floppy/USB-floppy emulation
mode, and legacy VGA. Write `zenithos.img` to a 1.44 MB floppy or a USB stick
with floppy geometry. Results vary by firmware; this is not a supported
configuration and there is no installer. Emulators are the supported path.

### 9. How do I build it?

Install **Node.js** and **NASM 2.16.01**, then run:

```bash
node build.js
```

No npm install is required for the OS build. The build locates NASM via the
`NASM` environment variable, `./nasm.exe`, `PATH`, or common install
directories, generates the trig table, compiles every module as a gate,
assembles boot + kernel, packs the image, validates it, and prints a SHA-256.
See the [quick start in the README](../README.md#quick-start).

### 10. Why Node.js for a NASM project?

Because building an OS image is orchestration: invoke NASM repeatedly, generate
a sine/cosine table, assemble the disk image, and verify it byte-for-byte.
Node's standard library does this with zero dependencies and runs everywhere
the contributors work. The OS itself has no runtime dependency on Node — the
image is pure machine code.

### 11. Does the build require npm packages?

No. The OS build (`node build.js`) uses only Node built-in modules. The
**headless test harness** (`node tools/emulate.mjs`, using v86) has dev-only
dependencies installed via `npm install`; that is optional and separate.

### 12. How do I run it?

```bash
qemu-system-i386 -fda zenithos.img -boot a
```

or, equivalently:

```bash
qemu-system-i386 -drive format=raw,file=build/zenithos.img,if=floppy -boot a
```

Click the window to focus it, then use the camera keys below.

### 13. What are the controls?

| Key | Action |
| --- | --- |
| `W` / `S` | Dolly in / out (zoom, clamped 130–560) |
| `A` / `D` | Yaw (rotate left / right) |
| `Q` / `E` | Pitch (tilt up / down, clamped ±6000) |
| `R` | Reset camera and auto-rotation |
| `C` | Toggle cinematic auto-rotation |
| `Space` | Boost (3× yaw, 2× pitch while held) |
| `↑` / `↓` | Mirror dolly in / out |
| `←` / `→` | Mirror yaw |

There is no mouse in v1.0. Keys are delivered by IRQ1, not polled. With
auto-rotation on (the default), the camera slowly orbits while you are not
holding a movement key.

### 14. Why does it boot into a neural mesh instead of a shell?

Because the project's core idea is that the system's state should be visible
and perturbable. A shell would work, but it would hide the machine behind a
command parser. The desktop *is* the system monitor: neurons, synapses,
packets, FPS, uptime, memory stats, and spike metrics are all rendered
directly from kernel state. A shell is planned for the POSIX era (v3.0), not
as a replacement for the visual identity.

### 15. Is there a filesystem, networking, or persistent storage?

No, no, and no. v1.0 has no storage driver and no network driver. Nothing
survives a reboot. VFS and a read-only filesystem are planned for v3.0.

### 16. Does it have userspace? Can I write applications?

Not yet. All code runs at CPL 0 in a single address space. There is no
process, no scheduler, no POSIX syscall ABI, and no app model. Ring 3, paging,
capabilities, and an IPC-based server model are the v2.0 milestone. The
`INT 0x80` gate in `src/arch/syscall.asm` exists with a small **native** call
table (ticks, serial write, SNN energy, heap used, PMM free KiB) that the boot
self-test exercises from ring 0 — it is a mechanism proof, not a userspace
API.

### 17. Is it secure?

Not in the sense of *isolating* untrusted code — see
[`SECURITY.md`](SECURITY.md) for the full threat model. v1.0's security
properties are supply-chain integrity (deterministic build, image read-back
verification, SHA-256), a validated boot hand-off, a tiny attack surface, and
diagnosable crashes. There is no paging, no ring 3, no W^X, and no signed
image. Do not deploy it as a security boundary.

### 18. Is it POSIX-compatible?

No. There are no POSIX syscalls, no libc, no processes, and no file
descriptors. The `INT 0x80` vector carries only a native 5-call table, not a
POSIX ABI. See [`POSIX-COMPATIBILITY.md`](POSIX-COMPATIBILITY.md) for the gap
analysis and the staged plan.

### 19. Why assembly instead of C or Rust?

Three reasons: the kernel must fit a 60 KiB window with no runtime; assembly
makes the memory map, interrupt path, and SNN arithmetic completely explicit;
and the project's purpose is to study the machine, not to hide it. The
trade-off is real: manual bounds checking and no compiler safety nets. A C or
Rust userspace is entirely plausible after v2.0 — the OS is not
assembly-for-ideology.

### 20. Does the SNN learn? Can I train it?

No. v1.0 uses a sparse weight matrix generated once at `snn_init` from a fixed
xorshift32 seed, then held constant for the lifetime of the boot. The network
has temporal dynamics, not learning. STDP-style learnable synapses are on the
v4.0 roadmap and will be deterministic and auditable. Training on user data is
an explicit anti-goal (the system collects none).

### 21. How do you know the SNN and the renderer are deterministic?

The model uses 8-bit integer state, a fixed-seed xorshift32 for ambient
stimulus and weight generation, a fixed update order, and the PIT tick as its
clock. The same build and the same input timeline produce the same spikes and
the same framebuffer. The build prints a stable image SHA-256, and the headless
harness captures frames for regression. Exact replay tooling lands in v1.1;
see [`TESTING.md`](TESTING.md).

### 22. How do I verify it without looking at the screen?

```bash
npm install          # dev dependencies for the harness only
node tools/emulate.mjs
```

The harness boots the image headlessly under v86, injects keys, captures
screenshots into `screenshots/`, and asserts that the frame is non-trivial
(not a uniform colour field). CI runs the same build and image checks.

### 23. Is it production-ready?

No. It is a research and education system. It lacks isolation, storage,
networking, a scheduler, SMP, ACPI/power management, and any certification.
"Do not use for safety-critical or multi-user systems" is not boilerplate — it
is the accurate status.

### 24. What does "Zenith" mean, and what's the licence?

"Zenith" is the point in the sky directly overhead — the highest point, the
apex of an arc. The project aims at a high standard from the bottom of the
stack: bare metal, no libc, no shortcuts. The source and documentation are
**MIT** licensed; see [`LICENSE.md`](../LICENSE.md).

---

*Something missing? Open an issue with the `question` label — good questions
get promoted into this file.*
