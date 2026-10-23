# ZenithOS Testing and Verification

> "It boots on my machine" is not a test. ZenithOS treats verification as part
> of the build: every image is validated before it is written, every module is
> assembled in isolation as a compile gate, every boot runs self-tests, and CI
> boots the image headlessly to prove the desktop actually renders. This
> document describes what is checked, what is *not* checked, and how to add a
> test.

---

## 1. Philosophy

Three rules:

1. **The build is the first test.** If the image cannot be passed through
   static verification, it should never reach an emulator.
2. **Every artifact has a hash.** Determinism is checked by content, not by
   trust. The build prints a SHA-256 and records it in a machine-readable
   report.
3. **Prove the pixels, not just the process.** A boot that reaches an idle
   loop with a black screen is a failure. The headless harness asserts that the
   captured frame contains real content.

No coverage percentage is claimed, because no instrumented coverage is
measured. The table below is the honest inventory.

---

## 2. Verification layers

```text
  source tree
      │
      ├─[1] config constants parsed          (KERNEL_ORG, KERNEL_SECTORS)
      ├─[2] generated assets                 (256 Q8.8 sin/cos, 256-colour palette)
      ├─[3] per-module NASM compile gate     (-f obj, -DZENITH_OBJ_CHECK)
      ├─[4] boot.asm assembled               (512 bytes, 0xAA55)
      ├─[5] kernel assembled flat            (≤ 61,440 bytes)
      │
      ├─[6] image packed                     (1,474,560 bytes)
      ├─[7] payload read-back                (boot + kernel + zero tail)
      ├─[8] SHA-256 + report.json            (provenance)
      │
      ├─[9] 10 boot self-tests               (boot-info, GDT, IDT, PMM, heap,
      │                                       SNN, RTC, PIT, VGA, INT 0x80)
      └─[10] headless v86 boot               (screenshots + frame assertion)
```

Layers 1–8 run in `node build.js`. Layer 9 runs inside the kernel on every
boot. Layer 10 runs in `node tools/emulate.mjs` and in CI.

---

## 3. Build gates (layers 1–8)

### 3.1 Toolchain discovery (layer 0)

- NASM is looked up in this order: `$NASM`, `./nasm.exe`, `./nasm/nasm.exe`,
  `./bin/nasm.exe`, common Windows install directories, `PATH`.
- NASM is probed with `-v`; a candidate that cannot start or returns non-zero
  is skipped.
- The selected assembler line is printed and stored in `build/report.json`.
- Missing NASM produces an actionable error, not a stack trace.

### 3.2 Config parsing (layer 1)

`build.js` reads `src/include/config.inc` as the single source of truth for:

- `KERNEL_ORG` — kernel load address (`0x10000`),
- `KERNEL_SECTORS` — CHS window (`120` sectors, 60 KiB).

A missing or unparsable constant is a fatal build error. This prevents the
loader and the kernel from silently disagreeing about the memory contract.

### 3.3 Generated assets (layer 2)

`build.js` generates two deterministic include files:

- `build/generated/trig.inc` — 256 angular steps over a full turn, Q8.8
  (`round(cos/sin * 256)`), written as `dw` rows, `align 16` before each table,
  consumed by `src/lib/fixed.asm`.
- `build/generated/palette.inc` — 256 RGB triples (0..63 per channel): fixed
  slots 0..15 for stable/panic-safe colours, neon ramps 16..191, and a dark
  tail. Consumed by `src/ui/theme.asm`.

Both are pure functions of constants with no randomness, so their SHA-256
hashes are stable; the build logs truncated hashes and records them in
`report.json`.

### 3.4 Per-module compile gate (layer 3)

Every `.asm` file under `src/` except the umbrella `src/kernel.asm` is
assembled independently:

```text
nasm -f obj -DZENITH_OBJ_CHECK -I src -I build/generated -o build/obj/<rel>.o <file>
```

Why this matters: the flat kernel build sees every symbol, so accidental
cross-module references would otherwise be invisible. With
`-DZENITH_OBJ_CHECK`, each module must explicitly `%ifdef` + `extern` what it
needs, forcing the dependency to be declared. The gate catches:

- syntax errors and invalid instructions in a module,
- undeclared cross-module references,
- missing include paths,
- assembler warnings (surfaced in the build log).

The gate can be skipped with `node build.js --no-check` for fast local
iteration, but CI always runs it.

### 3.5 Boot sector (layer 4)

After assembling `boot.asm` with `-f bin`:

- `boot.bin` must be exactly **512 bytes**,
- byte 510 must be `0x55` and byte 511 must be `0xAA`,
- the SHA-256 of the sector is recorded.

### 3.6 Kernel budget (layer 5)

- `kernel.bin` must be non-empty.
- It must be **≤ `KERNEL_SECTORS * 512`** (`120 × 512 = 61,440` bytes).
- The build prints `used / budget` and the fill percentage (e.g.
  `42117 / 61440 bytes (68.5% of window)`).
- Exceeding the budget is fatal: the bootloader would not load the full kernel,
  which would be far worse than a failed build.

### 3.7 Image assembly and read-back (layers 6–7)

The image is a deterministic byte layout:

| Offset | Content |
| --- | --- |
| `0x000000` | `boot.bin` (512 bytes) |
| `0x000200` | `kernel.bin` (≤ 61,440 bytes) |
| rest | zero fill to `1,474,560` bytes |

After writing `build/zenithos.img` (and a copy at the repository root), the
builder re-reads the file and checks:

1. total size is exactly **1,474,560** bytes (2,880 × 512),
2. bytes `[0, 512)` equal `boot.bin`,
3. bytes `[512, 512 + kernel.length)` equal `kernel.bin`,
4. every remaining byte is **zero** (no stale memory is shipped).

Any mismatch fails the build with a specific message
(`boot sector read-back verification failed`, etc.).

### 3.8 Provenance report (layer 8)

`build/report.json` contains:

- project/version, NASM version string,
- start/finish timestamps and duration,
- the list of modules with object sizes and status,
- boot/kernel artifact hashes and sizes,
- image geometry and SHA-256,
- boolean results for every check,
- the generated trig file's hash and entry count.

CI archives this file, and release notes quote the image hash from it.

---

## 4. Boot self-tests (layer 9)

The kernel runs `src/lib/selftest.asm` during initialisation, after `STI` and
before entering the render loop, and reports results through the unified logger
(serial + ring buffer). There are exactly **10** checks; the HUD renders
`SELFTEST passed/total`.

| # | Check | Assertion |
| --- | --- | --- |
| 1 | Boot-info hand-off | magic `0x4F4E455A` and version `1` at `0x0500` |
| 2 | GDT | `sgdt` base equals `gdt_start`, limit equals 23 |
| 3 | IDT | `sidt` base equals `idt_table`, limit equals 2047 |
| 4 | PMM | two page allocations are non-zero, distinct, and freeing both restores the free-page count |
| 5 | Heap | 64-byte allocation is 8-byte aligned inside the arena, canary write/read succeeds, free restores the free-byte count |
| 6 | SNN | 48 ticks produce at least one spike and energy stays ≤ `24 × 255` |
| 7 | RTC | second ≤ 59, minute ≤ 59, hour ≤ 23, month 1..12 |
| 8 | PIT delivery | the tick counter advances during a ~300,000-iteration spin |
| 9 | VGA back buffer | a byte at `0x20000` is writable and restore-able |
| 10 | `INT 0x80` | `SYS_TICKS` returns a non-zero tick value |

Failures are logged as `[ERROR] selftest: <name>`; the boot log and the HUD
therefore agree. A failed check does not itself halt the kernel in v1.0 except
where the subsystem is unusable (the log is the diagnostic), which is a known
cut corner: **v1.1 makes failed critical self-tests fatal**.

> Exact assertion sets and messages live in `src/lib/selftest.asm` and are
> described in [`ARCHITECTURE.md`](ARCHITECTURE.md#16-boot-self-tests). When the
> self-tests change, this table changes in the same pull request.

---

## 5. Headless emulator harness (layer 10)

Interactive QEMU is for humans; CI needs a headless, scriptable machine.
`tools/emulate.mjs` boots the image under **v86** (a JavaScript x86 emulator),
captures the VGA framebuffer, writes PNGs, and asserts properties of the
result.

### 5.1 What it does

1. Loads `build/zenithos.img` (or `--image <path>`).
2. Boots it in a Node-hosted v86 instance (no display required).
3. Waits for the kernel to reach its render loop (boot time plus a safety
   margin).
4. Injects camera keys through the emulated PS/2 keyboard controller:
   `W`/`S` dolly, `A`/`D` yaw, `Q`/`E` pitch, `R` reset, `Space` boost (the
   harness key map supports `w a s d q e r space`).
5. Captures the 320×200 framebuffer after boot and after input.
6. Writes PNG files from the emulator's RGBA framebuffer with a small
   dependency-free PNG encoder (`tools/lib/png.mjs`), optionally scaled 3×.
7. Asserts the frame is **non-trivial**: ≥ 16 distinct colours, ≥ 1% non-black
   pixels, and not a uniform fill — enough to distinguish "kernel rendered"
   from "black screen / emulator not ready / stuck in boot".

### 5.2 Commands

```bash
npm install                 # dev dependency (v86) — not needed for OS build
node tools/emulate.mjs      # boot build/zenithos.img, write screenshots/
```

Useful options (see `node tools/emulate.mjs --help`):

| Flag | Meaning |
| --- | --- |
| `--image <path>` | Boot an explicit image (default `build/zenithos.img`) |
| `--fixture` | Boot the harness self-test fixture instead of the OS |
| `--seconds <n>` | Guest run time before capture (default 6 s) |
| `--out <path>` | Screenshot output (default `screenshots/emu-boot.png`) |
| `--keys "w,w,d,r"` | Inject key presses; writes `<out>-before.png`/`-after.png` and prints a frame diff |
| `--serial <path>` | Capture COM1 output to a file |
| `--scale <n>` | Integer nearest-neighbour upscale for PNGs (default 3) |
| `--require-keys` | Fail (exit 3) if injected keys produce no pixel difference |
| `--allow-blank` | Exit 0 even when the frame gate fails (debug only) |
| `--timeout <n>` | Hard wall-clock timeout in seconds (default `seconds + 60`) |

Exit codes: `0` pass, `2` emulator fault/timeout, `3` blank or non-graphical
frame, `4` usage/I/O error.

`npm run test:emu` is the canonical entry point. `npm run test:fixture` builds
a small VGA fixture image and verifies the harness itself — useful when the real
kernel is mid-change.

### 5.3 Fixtures

`tools/fixtures/` contains a tiny bootable image builder and a VGA test
program. The fixture also exercises the boot path (mode 13h, palette, a known
pattern) so that a change to the harness or to the emulator version can be
distinguished from a change to the kernel. A failing `test:fixture` with a
passing kernel means the harness is broken, not the OS.

### 5.4 Frame assertions

The harness fails the run if:

- the image does not boot within the timeout,
- the framebuffer cannot be captured,
- the guest never enters a graphical mode,
- the frame is a single colour or uniform,
- fewer than 16 distinct colours are present,
- fewer than 1% of pixels are non-black,
- `--require-keys` is set and input injection produces no observable change.

It does not claim pixel-perfect regression yet; golden-image comparison is
planned for v1.1 (see §7).

**Measured v1.0 gate result:** 320×200, 100.00% non-black pixels, 61 distinct
colours, ~15–17% of pixels changed after WASD injection, and ~30–34 FPS under
the headless emulator. The FPS number is an emulator measurement, not a
hardware benchmark: the kernel paces frames to the 100 Hz PIT and is designed
for 100 fps on real hardware. These numbers are reported by the harness, not
stored as golden values.

---

## 6. CI

The GitHub Actions workflow is `.github/workflows/build.yml`. It runs on pushes
and pull requests to `main`, and can be triggered manually
(`workflow_dispatch`). It has two jobs on `ubuntu-latest` with Node 20: `build`
(required) and `visual-regression` (advisory).

```text
job: build
  ├── checkout (actions/checkout@v4)
  ├── setup Node 20 (actions/setup-node@v4)
  ├── apt-get install nasm
  ├── node build.js                    # layers 0–8
  ├── verify image contract            # 1,474,560 B image, 512 B boot,
  │                                    # kernel <= 61,440 B, 55 AA signature
  ├── sha256sum boot/kernel/image + cat build/report.json
  └── upload artifact: image, boot.bin, kernel.bin, report.json

job: visual-regression (needs: build, continue-on-error: true)
  ├── checkout + Node 20 + nasm
  ├── npm ci                           # v86 harness dependency only
  ├── node build.js
  ├── node tools/emulate.mjs \         # --seconds 10 --keys "w,a,s,d,r"
  │      --out screenshots/emu-ci.png --serial build/serial-ci.log
  ├── cat build/serial-ci.log (always)
  └── upload screenshots + serial log (always)
```

The build job is the release gate: if the image contract check fails, the CI is
red. The visual-regression job is intentionally `continue-on-error: true` in
v1.0 because v86 rendering timing is environment-sensitive; its logs and
screenshots are still uploaded on every run. It becomes a hard gate in v1.1
together with golden-frame comparison.

The artifact uploads mean a red run is diagnosable without reproducing it
locally. CI does not run on real hardware (see §7).

Planned matrix (v1.1):

- OS: `ubuntu-latest`, `windows-latest`, `macos-latest`,
- Node: 18.x, 20.x LTS,
- NASM: distribution package and pinned 2.16.01,
- assert the image SHA-256 is identical across all legs (reproducibility).

---

## 7. What is not tested yet

Honesty about gaps is part of the test plan:

- **Real hardware.** No CI runner has a floppy controller or legacy VGA. Boot
  on physical machines is reported by users, not asserted by CI.
- **Golden-image pixel diffs.** v1.0 asserts "non-trivial frame"; exact
  per-pixel regression is v1.1 work, because it requires pinning the emulator
  and palette precisely.
- **Deterministic replay.** Recording and replaying an input trace to
  reproduce a frame bit-for-bit is planned for v1.1.
- **Fault injection in CI.** Forcing `#DE`/`#UD` and asserting the panic text
  is planned for v1.1.
- **Fuzzing.** The scancode and boot-info parsers are small enough for
  exhaustive review now; a fuzz harness is planned when the POSIX layer
  arrives (v3.0) and for the boot-info decoder in v1.1.
- **Memory-safety instrumentation.** No ASan/Valgrind for a freestanding
  kernel; debug-build canaries and allocator checks are v1.1.
- **Performance benchmarks.** The HUD shows live FPS, and CI records frame
  timing, but no synthetic benchmark suite is claimed.
- **Security testing.** No penetration test or audit has been performed (see
  [`SECURITY.md`](SECURITY.md)).

---

## 8. How to add a test

**For a build invariant:**

1. Add the check to `build.js` (fail with a specific message).
2. Record the result in `report.checks`.
3. Add a sentence to §3 of this document.
4. Verify the check by temporarily breaking the artifact (e.g. pad the boot
   sector) and confirming a non-zero exit.

**For a kernel behaviour:**

1. Add an assertion to `src/lib/selftest.asm` or a focused test in the relevant
   module behind `ZENITH_OBJ_CHECK`/debug flags.
2. Log the result through the unified logger so it appears on serial and in the
   HUD.
3. Update the self-test table in §4.

**For a UI/frame behaviour:**

1. Extend `tools/emulate.mjs` with the input sequence and the property to
   assert (distinct colour count, lit-pixel ratio, change after input).
2. Keep the assertion robust across emulator versions; prefer structural
   properties over exact pixel values.
3. Commit the resulting screenshot when it is part of the expected evidence.

**For an image-format change:**

1. Update `build.js` and this document together.
2. Re-run `node build.js` twice and compare SHA-256.
3. Re-run the headless harness and confirm the frame still passes.

---

## 9. Reproducibility checklist

Run before a release:

```bash
node build.js && sha256sum zenithos.img   # or Get-FileHash on Windows
node build.js && sha256sum zenithos.img   # hashes must match
node tools/emulate.mjs                    # screenshots regenerate
```

Checklist:

- [ ] Two consecutive builds produce identical `zenithos.img` SHA-256.
- [ ] `build/report.json` checks are all `true`.
- [ ] Boot self-tests report no errors on the serial console.
- [ ] The headless harness captures a non-trivial frame.
- [ ] Injected keys change the frame (camera responds).
- [ ] Committed screenshots match the harness output.
- [ ] `README.md` and `docs/` still describe the actual behaviour.

---

## 10. Test inventory (v1.0)

| Layer | Test | Where | Automated |
| --- | --- | --- | --- |
| 0 | NASM discovery/version probe | `build.js` | Yes |
| 1 | config constant parsing | `build.js` | Yes |
| 2 | trig table generation + hash | `build.js` | Yes |
| 3 | per-module compile gate | `build.js` | Yes |
| 4 | boot sector size/signature | `build.js` | Yes |
| 5 | kernel non-empty + budget | `build.js` | Yes |
| 6 | image size | `build.js` | Yes |
| 7 | payload + zero-tail read-back | `build.js` | Yes |
| 8 | SHA-256 + report.json | `build.js` | Yes |
| 9 | 10 boot self-tests (boot-info/GDT/IDT/PMM/heap/SNN/RTC/PIT/VGA/INT 0x80) | kernel boot | Yes (in-guest) |
| 10 | headless boot + PNG capture | `tools/emulate.mjs` | Yes (CI) |
| 10 | key injection response | `tools/emulate.mjs` | Yes (CI) |
| 10 | non-trivial frame assertion | `tools/emulate.mjs` | Yes (CI) |
| — | fixture harness self-test | `tools/fixtures/` | Yes (manual/CI) |

---

*Verification is not a phase that happens after v1.0. It is the reason v1.0 can
be trusted to be honest about itself.*
