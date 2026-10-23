# ZenithOS Security

> **Scope note (v1.0).** ZenithOS v1.0 is a single-address-space, ring-0,
> no-paging operating system with no userspace, no filesystem, no network, and
> no persistent storage. It cannot host untrusted applications. The most
> important sentence in this document is this one: **v1.0 provides
> reliability-oriented safety, not security isolation.** Everything labelled
> *planned* is not implemented.

Security reports: see [§8 Responsible disclosure](#8-responsible-disclosure).

---

## 1. Security properties in one table

| Property | v1.0 | Target milestone |
| --- | --- | --- |
| Memory isolation between components | **None** — flat 4 GiB segments, single address space, no paging | v2.0 |
| Privilege separation (ring 3) | **None** — all code runs at CPL 0 | v2.0 |
| Attack surface from untrusted input | Keyboard scancodes and BIOS data only | v2.0 |
| Network attack surface | **None** — no network stack | v3.0 |
| Persistent storage attack surface | **None** — no filesystem | v3.0 |
| Signed images | **None** | v2.0 (design), v4.0 (product) |
| Measured/secure boot | **None** | v4.0 |
| W^X enforcement | **N/A** — no page tables | v2.0 |
| Stack canaries / bounds hardening | **None** | v1.1 (selected modules) |
| Reproducible build | **Yes** — deterministic NASM build, SHA-256 printed and verified | v1.0 (shipped), attestation v5.0 |
| Managed vulnerability disclosure | **Yes** — policy below | v1.0 (documented) |
| Fuzzing | **No** | v1.1 |

The honest summary: v1.0's security work is **supply-chain integrity and
crash-safety**, not containment of hostile code.

---

## 2. Threat model

### 2.1 Assets

What would an attacker want?

1. **Integrity of the boot image** — the bytes the CPU executes. If an
   attacker can change `zenithos.img`, they own the machine before any kernel
   code runs.
2. **Determinism of the neural model** — the project claims the SNN is
   auditable and reproducible; tampering breaks that guarantee and any future
   attestation built on it.
3. **Build integrity** — the mapping from source commit to image hash.
4. **Developer machines** — the build invokes NASM and, for the test harness,
   Node; a compromised toolchain compromises the artifact.
5. **Future user data** (post-v3.0) — files, credentials, model weights.
   **Nothing exists today.**

### 2.2 Adversaries

| Adversary | Capability | Relevance to v1.0 |
| --- | --- | --- |
| **Physical attacker** | Can write the floppy image, attach devices, probe DMA/MMIO | Primary threat; no mitigation in v1.0 beyond the boot checks |
| **Supply-chain attacker** | Can modify repo, CI, or release artifacts | Relevant: reproducible builds + SHA-256 help detection, not prevention |
| **Malicious input** (keyboard controller, BIOS data) | Can send arbitrary scancodes / corrupt boot info | Relevant but tiny surface; ring-0 parser must be robust |
| **Network attacker** | Remote | **Not applicable** — no network |
| **Malicious local application** | Runs code in the OS | **Not applicable** — no userspace, no application loading |
| **Insider on the build host** | Can change NASM or Node | Relevant: pinned tool versions, reproducible builds |
| **DMA-capable device** | Reads/writes physical memory directly | Relevant the moment non-boot devices are enabled; see §4.3 |

### 2.3 Trust boundaries

```text
  +---------------------------- untrusted ----------------------------+
  |  floppy image bytes   BIOS/firmware   keyboard controller   DMA   |
  +------------------------------+------------------------------------+
                                 |
                    +------------v-------------+
                    |   boot sector (16-bit)   |  <-- validates payload
                    +------------+-------------+
                                 | handoff: E820 + boot magic
                    +------------v-------------+
                    |  kernel, ring 0, flat    |  <-- all code trusted
                    |  no userspace boundary   |
                    +--------------------------+
```

There is exactly one privilege boundary in v1.0: **the boot sector trusting
what the BIOS and the disk hand it**. Everything after the protected-mode
jump is one trust domain.

---

## 3. Current mitigations (what v1.0 actually does)

### 3.1 Boot-path hardening

- **Boot signature validation.** `build.js` refuses to ship an image whose boot
  sector does not end in `0xAA55`, is not exactly 512 bytes, or exceeds the
  kernel budget.
- **Payload read-back.** After writing `zenithos.img`, the builder re-reads it
  and compares both the boot sector and the kernel region byte-for-byte
  against the freshly assembled binaries. A truncated or misaligned image
  fails the build rather than being committed.
- **Deterministic image layout.** Boot sector at LBA 0, kernel at LBA 1, image
  zero-padded to exactly 1,474,560 bytes. There is no uninitialised memory in
  the file.
- **Kernel budget enforcement.** The CHS load window is fixed at 60 KiB;
  the build fails if the kernel overflows it, preventing silent partial loads.
- **BIOS hand-off block validation.** The boot sector writes a magic/version
  block (magic `ZENO`, `BOOTINFO_VERSION 1`) at `0x0500`; the kernel checks the
  magic before trusting any E820 entry.
- **Failure paths halt loudly.** `DISK FAIL` and `A20 FAIL` halt the CPU in
  real mode rather than continuing with a corrupt environment.
- **No BIOS calls after the protected-mode jump.** Firmware interfaces are
  abandoned once the kernel starts, removing an entire class of confused-deputy
  bugs.
- **Assets:** `src/include/config.inc` is the single source of truth for the
  loader/kernel memory contract, reducing drift between the two halves.

### 3.2 Runtime robustness (crash-safety, not security)

- **Double buffering** prevents partial frames and reduces timing-dependent
  visual corruption.
- **Bounded keyboard ring buffer** (64 entries) with wrap handling instead of
  unbounded growth.
- **Static memory map** — no paging means no page-fault side channels, but also
  no isolation; buffers are fixed-size and reviewed by hand.
- **Boot self-tests** for the boot-info hand-off, GDT, IDT, PMM, heap, SNN,
  RTC, PIT interrupt delivery, VGA back buffer, and the `INT 0x80` ABI surface
  broken invariants early, before the UI starts.
- **Panic path** for CPU exceptions prints the vector name, error code,
  faulting `EIP`, `CR2`, and `CR0` (with `CR3` captured) to the screen and the
  full record to serial, then halts deterministically. A crash is diagnosable
  rather than silent.
- **Unified logger** to COM1 plus an in-memory ring buffer; useful for
  triage, with the caveat that logs are volatile.
- **100 Hz monotonic tick** for timing; no reliance on wall-clock ordering for
  correctness.

### 3.3 Build and supply-chain integrity

- No npm dependencies in the OS build; only Node's built-in modules.
- NASM is located via `NASM`, the repo root, or `PATH`; the version is printed
  in the build log.
- The build prints the SHA-256 of the final image; identical sources produce an
  identical hash.
- The OS image contains only project-authored assembly; no third-party blobs.
- CI builds from a clean checkout (`build.yml`).

### 3.4 What v1.0 deliberately does *not* claim

- It is **not** a sandbox, container, hypervisor, or secure enclave.
- It is **not** hardened against physical DMA attacks.
- It does **not** verify a signature before booting; anyone who can write the
  image can run code.
- It does **not** isolate kernel subsystems; a bug anywhere (including the SNN)
  runs with full privileges.
- It is **not** suitable for multi-user or safety-critical use.

---

## 4. Exploit surface analysis

The attack surface is unusually small for an OS because the OS has almost no
interfaces. That is the security advantage of minimalism — and the reason the
project is candid that the advantage disappears at v2.0 unless isolation is
done properly.

### 4.1 Inputs the kernel parses

| Input | Source | Trust | Risk |
| --- | --- | --- | --- |
| Scancodes | 8042 keyboard controller, port `0x60` / IRQ1 | Untrusted | Ring-buffer bounds; table lookup must be total |
| E820 memory map | BIOS `INT 15h E820`, copied to `0x0500` | Semi-trusted | Kernel must validate entry count and types before building the PMM bitmap |
| RTC/CMOS registers | Ports `0x70`/`0x71` | Semi-trusted | BCD conversion and range checks; never used for security decisions |
| Serial status | COM1 `0x3F8` | Host-controlled | Output-only in v1.0; no parser |
| PIT ticks | 8254 | Trusted hardware | Counter wraparound handled arithmetically |

There is no filesystem parser, no network parser, no ELF loader, no font file
parser, and no model file parser in v1.0. Each of those is a future attack
surface and is budgeted for fuzzing before it ships.

### 4.2 Privileged interfaces

| Interface | v1.0 | Notes |
| --- | --- | --- |
| `INT 0x80` | Software trap | Minimal native call table (5 calls: ticks, serial write, SNN energy, heap used, PMM free KiB), DPL-3 gate, self-tested from ring 0 | No user mode exists yet; pointer arguments are not validated for user addresses |
| Port I/O | Kernel-wide access to `0x60`, `0x64`, `0x3F8`, `0x40–0x43`, `0x20–0x21`, `0x70–0x71`, `0x3C8–0x3C9` | Ring 0 only; future capability check |
| MMIO | VGA `0xA0000` | Ring 0 only |
| Interrupts | IDT gates for exceptions, IRQs, `INT 0x80` | Installed by the kernel; vectors named in panic output |

### 4.3 Physical attacks and DMA

- **Floppy image replacement** is trivially available to a physical attacker.
  v1.0 has no signature check. Mitigation roadmap: signed images (v2.0 design,
  v4.0 product), measured boot with a TPM-like root of trust (v4.0).
- **DMA** — the moment a bus-mastering device is enabled, an attacker with
  device access can read/write any physical address. v1.0 enables no
  bus-mastering drivers, but the plan must include an IOMMU or restricted DMA
  windows *before* such drivers ship. This is a hard requirement for v2.0's
  device-server work.
- **Cold-boot / memory probing** — all state is in DRAM with no encryption.
  Out of scope until a product defines a threat model that requires it.

### 4.4 Non-security bugs that can become security bugs

- **Unbounded loops** in rendering could hang the system (availability).
  Watchdog planned in v1.1.
- **Fixed-size buffers** in the logger and keyboard ring: off-by-one errors
  would corrupt kernel state. Code review + assertion coverage; fuzzing plans
  below.
- **SNN arithmetic** uses 16/32-bit fixed point: overflow must be bounded by
  construction and checked by self-tests.

---

## 5. Hardening roadmap

### 5.1 v1.1 — robustness and observability

- Panic forensics: symbol map, stack dump, backtrace, stable panic hash.
- Watchdog for stalled main loop.
- Compile-time assertions for buffer sizes and structure layouts.
- Fuzz harness for the only real parser in the system (scancode handling) and
  for the boot-info decoder; run under v86 in CI.
- Deterministic replay so crashes are reproducible from a trace.
- Optional debug build with extra padding/poison on stack and heap.
- Security-focused review checklist added to the PR template.

### 5.2 v2.0 — isolation (the big one)

- **Paging** with per-process address spaces; kernel mapped read-only to user
  where possible, with a separate kernel stack per task.
- **Ring 3** entry/exit through validated trap gates; no direct user pointers
  in kernel data structures.
- **W^X** page permissions; no executable stacks; no writable code.
- **Capability model** as the *only* authority mechanism: every resource
  (memory window, endpoint, device register range, model handle) requires a
  capability with an explicit rights mask.
- **Validated syscall ABI** with a version table, argument copy-in/copy-out,
  bounds checks, and typed errors.
- **DMA isolation** through an IOMMU abstraction or restricted IOMMU-less
  windows before any bus-master driver ships.
- **ASLR for user tasks** (modest, given 3 GiB address space); canaries and
  guard pages in the userspace runtime.
- **Kernel self-protection:** read-only kernel text (via paging), null-page
  unmapped, SMEP/SMAP where the CPU supports it, CR0.WP enabled.

### 5.3 v3.0 — system-level hardening

- Filesystem integrity: checksummed metadata, read-only root by default,
  signed packages.
- Network stack: fuzzed parsers, no ambient access to sockets, capability-
  gated networking, mandatory rate limits.
- POSIX layer: syscall filtering, per-process limits, rlimits.
- Auditable boot log delivered to userspace through a read-only log server.

### 5.4 v4.0 — platform trust

- **Signed images:** detached Ed25519 (or similar) signatures over the image,
  verified by a verified loader; key rotation policy.
- **Measured boot:** hash chain of boot components recorded and exposed for
  attestation; recovery mode when verification fails.
- **Secure update:** A/B image slots with rollback protection where storage
  exists.
- **Hardware root of trust** for a reference board; documented threat model
  against physical attacks.
- **Model signing** for the AI runtime; refuse unsigned or out-of-budget
  models.
- **Independent security audit** before any v4.x product claim.

### 5.5 v5.0 — process

- Private security team with published membership.
- Coordinated disclosure SLAs with downstream distributors.
- Annual third-party audit and public remediation tracking.
- Bug bounty or funded security research programme.
- SBOM per release; reproducible-build attestations (SLSA-style) where the CI
  platform supports it.

---

## 6. Security invariants we intend to test

These become executable assertions as the milestones land:

1. No user-space pointer is dereferenced by the kernel without validation.
2. No writable mapping is executable; no executable mapping is writable.
3. Every capability-mediated operation checks rights before performing I/O.
4. Every syscall returns a typed error for malformed arguments and never
   panics on user input.
5. The kernel image is byte-identical when rebuilt from the same commit with
   the pinned toolchain.
6. The PMM never hands out the same physical page twice; freeing a page twice
   is detected in debug builds.
7. The heap allocator detects double-free and canary corruption in debug
   builds.
8. A fault in any userspace server cannot take down the kernel.

---

## 7. What to do if you think you found a vulnerability

Please **do not** open a public issue with exploit details. Use one of:

1. **GitHub Security Advisory** — the repository's *Security → Advisories →
   Report a vulnerability* flow (preferred; encrypted in transit and private
   to maintainers).
2. If advisories are unavailable, contact the lead maintainer through the
   private address published in the repository profile and mark the message
   `[ZENITHOS SECURITY]`.

Include: affected version/commit, a description of the issue, reproduction
steps or a minimal image/trace, your assessment of impact, and whether you
want credit.

### 7.1 Our commitments

| Stage | Target |
| --- | --- |
| Acknowledgement | within 72 hours |
| Triage and severity assessment | within 7 days |
| Fix or documented mitigation | within 90 days for high/critical issues |
| Public advisory | after the fix ships, coordinated with the reporter |
| Credit | offered by name or handle unless you prefer anonymity |

We will not pursue legal action against good-faith research, we will not
require an NDA, and we will not ask you to stay silent after the fixed
release is public. Please avoid privacy violations, data destruction, and
denial-of-service testing against systems you do not own.

### 7.2 Scope

In scope: boot path, kernel modules, build pipeline, emulator harness, CI
configuration, and documentation that makes security claims.

Out of scope *for v1.0*: anything requiring userspace, networking, or storage
(none exist), third-party toolchains (NASM, Node, QEMU, v86) unless the issue
is in our integration, and social-engineering attacks.

---

## 8. Responsible disclosure

The policy above is the responsible-disclosure policy for the project. It is
intentionally simple: private report, fast acknowledgement, coordinated fix,
public credit. A formal advisory template and a security.txt file will be
published with v1.1.

---

## 9. Summary

v1.0 is a **trusted single-address-space system** whose security story is
honest: tiny attack surface, deterministic artifact, validated boot hand-off,
diagnosable crashes — and no isolation whatsoever between components. That
last fact is the single most important input to the v2.0 microkernel design.
Anyone who needs containment, multi-user separation, or a hardened platform
should not use v1.0; anyone who wants to understand how such a platform could
be built incrementally is in the right place.

*Security review status: documented v1.0, no external audit.*
