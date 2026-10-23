# ZenithOS POSIX Compatibility

> **Honest headline:** ZenithOS v1.0 is **not POSIX-compatible**. It cannot run
> a POSIX process because it has no userspace, no process model, no filesystem,
> and no `fork`/`exec`/`wait`/`signal`. What v1.0 does have is the *groundwork*:
> a trap gate wired as the future system-call vector (`INT 0x80`) and a kernel
> designed to grow into a POSIX subsystem without abandoning its microkernel
> direction. This document is a gap analysis and a staged plan — not a
> compatibility claim.

---

## 1. Where v1.0 stands

| POSIX capability | v1.0 status | Detail |
| --- | --- | --- |
| Ring-3 userspace | ✗ Absent | All code runs at CPL 0 in one address space |
| Process model (`fork`, `exec`, `wait`) | ✗ Absent | No process abstraction, no scheduler beyond interrupt handlers |
| Address-space isolation | ✗ Absent | No paging/MMU use; flat segments |
| System-call ABI | ~ Native groundwork | `INT 0x80` gate (DPL 3) with a 5-call **native** ABI (ticks, string write, SNN energy, heap used, PMM free KiB); exercised by the boot self-test from ring 0. **No POSIX calls and no user mode.** |
| File descriptors | ✗ Absent | No file abstraction |
| Filesystem / VFS | ✗ Absent | No storage of any kind |
| `open`/`read`/`write`/`close` | ✗ Absent | Only kernel-internal drivers |
| `pipe`, `dup`, `fcntl` | ✗ Absent | — |
| Signals | ✗ Absent | Only CPU exception handling (a different mechanism) |
| `mmap`/`brk` | ✗ Absent | Physical bitmap + kernel heap only |
| `time()` / clocks | ✗ Absent as POSIX API | RTC and PIT exist inside the kernel |
| `termios`/tty | ✗ Absent | Keyboard ring buffer is kernel-internal |
| C library | ✗ Absent | No libc, no crt0, no dynamic loader |
| Shell/userland | ✗ Absent | Desktop loop is in-kernel |
| POSIX threads | ✗ Absent | No threads at all |
| Environment / `getenv` | ✗ Absent | No process environment |

Legend: ✓ shipped, ~ groundwork only, ✗ absent.

**What the `INT 0x80` groundwork means, precisely.** `src/arch/syscall.asm`
installs a DPL-3 gate for vector `0x80` (so a future ring-3 caller needs no new
vector) and dispatches a small **native** call table:

| `EAX` | Call | Arguments | Returns |
| --- | --- | --- | --- |
| `0` | `SYS_TICKS` | — | system ticks (`pit_ticks`) |
| `1` | `SYS_WRITE` | `EBX` = pointer, `ECX` = length | bytes written to COM1 |
| `2` | `SYS_SNN_ENERGY` | — | current SNN activation energy |
| `3` | `SYS_HEAP_USED` | — | kernel heap bytes in use |
| `4` | `SYS_PMM_FREE_KB` | — | free physical KiB |

Unknown call numbers return `-1` (`0xFFFFFFFF`). The boot self-test invokes
`SYS_TICKS` from ring 0 to prove the gate and dispatch path. These calls are
**not POSIX**, have no stability guarantee, and their pointer arguments are not
validated for a user address space (there is no user address space). Treat them
as a kernel-internal proof of mechanism. The POSIX ABI will be designed in
v2.0 together with ring 3 and published in this document before any POSIX call
is exposed.

---

## 2. Why POSIX at all?

ZenithOS does not want to become a general-purpose Unix clone. The POSIX path
is chosen for three concrete reasons:

1. **Ecosystem leverage.** A conforming libc and syscall layer instantly makes
   thousands of command-line tools, language runtimes, and test suites viable
   as workloads. That is the fastest way for a small OS to become *useful*
   rather than merely impressive.
2. **Specification discipline.** POSIX gives us an externally defined contract
   with well-understood semantics (and decades of bug reports). Implementing a
   documented subset is a better forcing function than inventing ecumenical
   APIs from scratch.
3. **Testability.** Conformance suites exist. "We implemented `open`" becomes
   "these 47 `open` tests pass and these 3 are documented deviations."

It also carries a risk: the gravity of POSIX can turn a principled microkernel
into a monolithic Unix. The plan therefore keeps POSIX **outside the trusted
core**, as a userspace subsystem on top of the capability-based kernel.

---

## 3. Target architecture

```text
+---------------------------------------------------------------+
|                      application / shell                      |
+---------------------------------------------------------------+
|         libc (musl) — malloc, stdio, pthreads stubs           |
+---------------------------------------------------------------+
|   POSIX server (userspace): fd table, process bookkeeping,    |
|   signals, VFS client, tty discipline, time service client    |
+---------------------------------------------------------------+
|      IPC: capability-addressed endpoints (send/receive)       |
+---------------------------------------------------------------+
|  microkernel (v2.0): paging, ring 3, capabilities, scheduler  |
|  servers: VFS | tty | clock | logger | display | device       |
+---------------------------------------------------------------+
```

Three rules make this work:

- **No POSIX knowledge in the kernel.** The kernel understands threads,
  address spaces, endpoints, and capabilities — not file descriptors.
- **The POSIX server is restartable.** If it crashes, the kernel survives; the
  test suite asserts this.
- **The syscall ABI is thin.** User code traps into the kernel only for
  operations that genuinely need kernel authority; everything else is IPC to a
  server.

---

## 4. Staged plan

### Stage P0 — v1.0 (shipped): groundwork

- `INT 0x80` gate installed with DPL 3, plus a minimal **native** call table:
  ticks, serial write, SNN energy, heap used, PMM free KiB.
- Boot self-test #10 exercises the gate from ring 0.
- No ring 3; no process; no POSIX API; no ABI stability commitment.
- Deliverable: a kernel whose interrupt architecture can host a syscall entry
  without redesign.

### Stage P1 — v2.0: processes and syscalls

- Paging + ring 3 + per-task kernel stacks.
- A versioned syscall table: second-level dispatch, argument copy-in/copy-out,
  bounds validation, typed errors (`-EFAULT`, `-EINVAL`, `-ENOSYS`).
- Thread creation, address-space creation, exit, yield, wait primitives
  (kernel-level, not yet POSIX-shaped).
- A minimal userspace runtime (`crt0`, TLS if needed) and a `write`-like debug
  output call so tests can run applications.
- **Exit:** a userspace program prints to the serial console and exits; a
  second program is scheduled; a faulting program is killed without harming
  the kernel.

### Stage P2 — v3.0: POSIX core

Implement in this order, because each layer depends on the previous one:

1. **Time and identity:** `clock_gettime`, `nanosleep`, `getpid`, `getppid`,
   `getuid`/`geteuid` (single-user model), `uname`.
2. **Memory:** `brk`, `mmap`/`munmap` (anonymous first, file-backed later),
   `mprotect`.
3. **Basic I/O:** `open`, `close`, `read`, `write`, `lseek`, `stat`/`fstat`,
   `dup`/`dup2`, `pipe`, `fcntl` (subset).
4. **VFS:** a POSIX server translating paths to the in-memory VFS and the
   read-only root image; mount points; `readdir`, `mkdir`, `unlink`.
5. **Processes:** `fork` (COW), `execve` (flat binary first, ELF32 later),
   `waitpid`, `_exit`, `kill`, process groups/sessions.
6. **Signals:** `sigaction`, `sigprocmask`, `kill`, `raise`; a documented
   subset of default actions; signal delivery at safe points.
7. **Terminal:** tty discipline for the keyboard/VGA console, `termios`
   subset, line editing in the POSIX server.
8. **Files and metadata:** permissions (single-user mode), timestamps,
   `chdir`/`getcwd`, symbolic-link stub.

**Exit criteria:** the conformance matrix below is populated for the subset;
`ls | cat` works; a statically linked musl "hello world" and the busybox subset
run; two processes are isolated.

### Stage P3 — v3.x: libc and ecosystem

- Port **musl** first (MIT, clean design, static linking).
- Provide `crt0`, `syscall()` wrappers, and a `pthread` stub (threads may be
  unsupported initially; return `ENOSYS`, never lie).
- Port a curated busybox subset as the reference userland.
- Add a POSIX test-suite job to CI (see §6).
- Publish per-call status in this document; treat the table as the API
  contract.

### Stage P4 — v4.0: completeness and performance

- Full POSIX 2008 subset (not the optional realtime extensions unless
  justified).
- ELF32 dynamic loading and shared libraries (budgeted, not promised).
- Robust `fork` for multi-threaded parents (documented limitations).
- Event notification (`poll`/`select`) via the IPC layer.
- Performance work: syscall fast path, zero-copy pipes, page-cache design.

---

## 5. Conformance targets

The project targets a **documented POSIX.1-2008 subset**, not full
certification. The table is the contract; a call is "supported" only when it
has tests and no known severe deviation.

| Area | Calls planned | Target stage | Status |
| --- | --- | --- | --- |
| Process creation | `fork`, `execve`, `wait`, `waitpid`, `_exit` | P2 | Not started |
| Process info | `getpid`, `getppid`, `getuid`, `geteuid`, `uname` | P2 | Not started |
| Signals | `sigaction`, `sigprocmask`, `kill`, `raise` | P2 | Not started |
| Memory | `brk`, `mmap`, `munmap`, `mprotect` | P2 | Not started |
| File I/O | `open`, `close`, `read`, `write`, `lseek`, `fstat`, `stat` | P2 | Not started |
| Directory | `opendir`, `readdir`, `mkdir`, `rmdir`, `unlink`, `chdir`, `getcwd` | P2 | Not started |
| Pipes / dups | `pipe`, `dup`, `dup2`, `fcntl` (subset) | P2 | Not started |
| Time | `clock_gettime`, `nanosleep`, `time`, `gettimeofday` | P2 | Not started |
| Terminal | `isatty`, `tcgetattr`, `tcsetattr`, `read`/`write` on tty | P2 | Not started |
| Polling | `poll`, `select` | P3 | Not started |
| Threads | `pthread_create` etc. | P3/P4 | Not started |
| Realtime | `timer_create`, `mq_open`, etc. | Not planned | Non-goal for v3.x |

**Non-goals for the POSIX subsystem (v3.x):** full realtime extensions,
SysV IPC, `aio`, NFS, dynamic linker complexity beyond what the reference
workloads need, multi-user permissions enforcement (single-user until a
credential model exists), and binary compatibility with Linux ABIs.

---

## 6. Conformance testing plan

1. **Unit tests** for each syscall wrapper (argument validation, error codes,
   edge cases) running in a userspace test harness.
2. **Curated upstream suites:** the musl test suite (functional subset), the
   Open POSIX Test Suite (selected areas), and a project-owned "posix-smoke"
   suite for boot-time execution under v86.
3. **Differential testing:** where semantics are subtle (e.g. `fork` + `pipe`
   + `SIGPIPE`), compare output against Linux in a container.
4. **Negative tests:** malformed pointers, double-close, invalid fds,
   `EINTR` behavior, signal-safety.
5. **CI job:** `posix-conformance` builds a test image, boots it headlessly,
   and fails on any regression; the job is advisory until v3.0-beta, required
   at v3.0.
6. **Published matrix:** every call in §5 links to its test status and known
   deviations. No call is marked supported without a passing test.

---

## 7. Known deviations and design choices

These will be documented as permanent deviations where appropriate:

- **No `fork` on a constrained memory budget** — v3.0 will use COW, but a
  `vfork`-style path may be required for very small systems; `fork` behavior
  on failure is documented.
- **Single user** — `getuid()` returns 0 until a credential model exists;
  permissions are enforced structurally (capabilities), not by uid.
- **No `/proc` as a filesystem initially** — process introspection will be an
  IPC API first; a `/proc`-shaped view may follow.
- **No `SIGSTOP`-based job control in the first shell** — a documented subset.
- **No `termios` raw-mode perfection** — the first tty implements canonical
  mode with line editing; raw mode arrives with the full tty server.

---

## 8. Why not just port Linux?

A fair question. Porting an existing kernel would deliver POSIX compatibility
immediately but defeat the project's purpose: a comprehensible, AI-native,
reliability-first kernel with an auditable trusted core. The POSIX layer is
valuable *because* it is a clean-room implementation on top of a different
security model; that is what makes it a contribution rather than a
reimplementation. The project stays honest about the cost: POSIX compatibility
is a multi-year effort, and it is scheduled accordingly.

---

## 9. How to help

- Write conformance tests before implementations (they define the contract).
- Port small utilities and report missing syscalls with minimal reproducers.
- Review the syscall table for semantic traps (`EINTR`, signal delivery
  windows, fd lifetime).
- The detailed ABI will live in `docs/ARCHITECTURE.md` and, once ring 3
  exists, in a generated header (`include/zenith/syscall.h`) checked into the
  repository.

---

*Status: v1.0 — native ABI groundwork only; no POSIX calls, no userspace. This
document will be rewritten at v2.0 with the syscall table and at v3.0 with real
conformance results.*
