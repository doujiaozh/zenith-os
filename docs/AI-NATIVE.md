# ZenithOS Is AI-Native

> An essay on why the neural network lives in kernel space, what "deterministic
> inference" means in assembly, and how input, model, and pixels form one
> closed loop. Written against the v1.0 source; every claim below is either
> implemented or explicitly marked as future work.

---

## 1. The claim, and the claim it is not

"AI-native operating system" is a phrase that usually means one of three
things: an OS that ships a chatbot, an OS with a vendor cloud integration, or
an OS whose startup screen mentions AI. ZenithOS means something narrower and
more literal:

> The intelligence is a **kernel subsystem**. It runs in ring 0, consumes
> hardware interrupts, maintains its own state, and its output directly drives
> what the machine displays. Remove it and the system is a different system.

ZenithOS v1.0 implements a deterministic **spiking neural network (SNN)** of
24 neurons inside the kernel. It is not a language model. It has no training
loop, no weights downloaded from the internet, and no notion of semantics. It
is a small dynamical system whose activation pattern is the desktop's visual
state.

The claim it is **not**: v1.0 does not run an LLM, does not learn, does not
understand anything, and is not "AGI in 8,704 bytes". The honest description
is *a bio-inspired controller embedded in the kernel to make the system's
state legible and perturbable*.

---

## 2. Why a spiking network, and why in the kernel?

### 2.1 Why spiking

A classic artificial neuron computes a weighted sum and applies an activation
function. A spiking neuron accumulates membrane potential and emits a discrete
event — a spike — when the potential crosses a threshold, then resets. This
has three properties that matter on bare metal:

1. **Discrete output.** A spike is a bit. It can drive a counter, a brightness,
   a pulse animation, or a log event without floating-point conversion.
2. **Temporal memory.** The leak term makes the network's state depend on how
   recently stimuli arrived, not just how many. The 100 Hz PIT gives a natural,
   hardware-clocked time base.
3. **Cheap arithmetic.** Leaky integrate-and-fire needs add, shift, compare,
   and a saturating minimum — 8-bit integer operations. No libm, no floating
   point, no `exp()`. That is compatible with the no-libc, no-FPU-assumptions
   kernel.

There is no training, no refractory period, and no inhibition in the v1.0
model. It is a small excitatory recurrent system designed for legible
dynamics, not a biologically faithful simulation.

### 2.2 Why in the kernel

There are three defensible answers, and one honest one.

- **Latency.** The model is evaluated inside the same interrupt/render loop as
  the framebuffer. There is no context switch, no syscall, no IPC hop between
  "sensor" and "inference" and "actuator". The cost is a few hundred cycles
  per tick.
- **Determinism.** In userspace, a scheduler, signals, and allocation timing
  could perturb the model. In the kernel, the update order is fixed: update
  neurons in index order, no preemption inside the update, no allocator.
- **Observability.** The kernel already owns the logger and the HUD. Spike
  counts, activations, and model state can be displayed and logged without a
  debugger.

The honest answer: **in v1.0 the model is in the kernel because there is no
userspace to put it in.** The kernel is a single trust domain. The roadmap's
v2.0 microkernel moves the model into a userspace AI server once isolation,
capabilities, and IPC exist (see `ROADMAP.md#v20--microkernel`). The v1.0
placement is a deliberate simplification and a known limitation for security,
not a permanent architectural claim.

---

## 3. The v1.0 model

### 3.1 Topology

- **24 neurons**, each with a single byte of activation (`0..255`). There is no
  layer split or back-propagation: every neuron is both input and recurrent
  substrate, and the same 24 values drive the whole visual state.
- **Inputs:** the 100 Hz timer tick is the model clock. The network also
  receives a pseudo-random "ambient" stimulus (see below) and extra stimulus
  from keyboard events: `W`/`A`/`S`/`D` map to neurons `0..3`, `Q`/`E` to
  neurons 4/5, `Space` to neuron 7, each injecting `+96` (saturating). That is
  why the desktop visibly reacts to input.
- **Connectivity:** a sparse excitatory 24×24 weight matrix, generated once at
  `snn_init` from a fixed xorshift32 seed. Low 6 bits of each random draw give
  the weight (0–40; a zero means "no synapse"), and the top 2 bits gate
  connectivity. Roughly three quarters of the entries are non-zero.
- **Statistics:** `snn_spike_count` (total spikes since boot),
  `snn_spikes_this_tick`, and `snn_energy` (sum of the 24 activations, bounded
  by `24 × 255 = 6120`). `SNN_MAX_SPIKES` (255) documents the saturating
  counter ceiling used by the UI.
- **Threshold:** activation reaching `SNN_THRESHOLD` (192) fires a spike.
- **Determinism:** the only pseudo-randomness is xorshift32 with a fixed seed
  (`0x13579BDF`), advanced once per tick for ambient stimulus and used once at
  init for weights. Two machines with the same build and the same input
  timeline produce the same spike train and the same frames.

### 3.2 Update semantics (leaky integrate-and-fire, integer)

Once per timer tick, in a fixed order:

```text
1. clear spike flags for all 24 neurons
2. leak:        a[i] = a[i] - (a[i] >> 3)          (12.5% decay per tick)
3. ambient:     r = xorshift32(); if (r & 7) == 0:
                    target = r mod 24
                    a[target] = min(a[target] + 72, 255)
4. fire:        for i = 0..23:
                    if a[i] >= 192:
                        a[i] = 0; spiked[i] = 1
                        spike_count++; spikes_this_tick++
                        for j = 0..23:
                            a[j] = min(a[j] + W[i][j], 255)
5. energy:      snn_energy = sum(a[0..23])
```

All arithmetic is saturating unsigned 8-bit (with a 32-bit sum at the end), so a
runaway network cannot fault the kernel. The leak and stimulus constants are
part of the v1.0 behavioural contract: changing them changes the frame hashes
in the regression suite.

### 3.3 What the activations drive

The UI does not read "the answer". It reads **state**:

- **Neuron glow and radius** scale with recent activation — active neurons look
  hotter.
- **Synapse brightness** follows the source and destination activity, so paths
  that just spiked flash.
- **Travelling packets** are launched along edges when a spike propagates.
- **HUD spike metrics** report the total spike count and the activity level.
- **The network's rhythm** (a slow oscillation from the leak dynamics and the
  ambient stimulus) gives the desktop its breathing quality.

This is the important design inversion: the SNN is not an application
displaying its output in a window. **The SNN's state is the desktop.** That is
what makes the OS AI-native rather than AI-adjacent.

---

## 4. The closed loop

```text
   hardware timer (100 Hz) ──────┐
                                 v
   keyboard IRQ1 ──> ring buffer ──> SNN update ──> activations
                                                      │
                                                      v
                            3D projection + palette energy per neuron/edge
                                                      │
                                                      v
                              back buffer (320x200) ──> VGA framebuffer
                                                      │
                                                      v
                                  human sees the mesh, presses a key
                                                      │
                                                      └────> back to input
```

Each element of the loop is deterministic and local:

1. **Perception** — timer ticks and key transitions are the only senses.
2. **Inference** — one SNN update per tick; no allocation, no blocking.
3. **Action** — activations modulate rendering parameters.
4. **Feedback** — the human is another node in the loop; keys are stimulus.

There is no training in the loop and no network call out of the machine. The
"learning" that a user perceives is dynamical state, not weight updates. This
distinction is central to the privacy story: nothing leaves the machine
because nothing needs to.

---

## 5. Determinism as a feature

Floating-point neural networks are difficult to reproduce exactly across
compilers, CPUs, and runtimes. An integer SNN in assembly has no such problem:

- All state is fixed-width integers.
- Weights are constants in the `.text`/`.data` output of NASM.
- The update order is a fixed loop.
- Time is the PIT tick, not wall-clock time.
- Inputs are recorded as scancodes and tick indices.

Consequences:

- **Reproducible frames.** The same build plus the same input trace yields the
  same framebuffer bytes. The deterministic seed is the only "noise", and it is
  a constant in the source. The v1.0 regression harness hashes frames; future
  releases will replay traces (v1.1) to reproduce any frame exactly.
- **Auditable behaviour.** Anyone can read the network in assembly and predict
  what a spike train will do. There is no opaque weight file; the weight matrix
  is synthesised from a documented seed and generation rule.
- **Testable inference.** Self-tests can assert spike counts after a fixed
  number of ticks with no input, and after a known stimulus pattern.

Determinism is not a performance trick. It is the property that makes an
in-kernel AI component reviewable, and reviewability is a prerequisite for
trust.

---

## 6. Performance characteristics

The SNN is deliberately small because the frame budget is 10 ms at 100 Hz and
the whole system must fit in 60 KiB. The design targets:

- 24 neurons × 24 weight entries, but the propagation loop only runs for
  neurons that actually fired on that tick.
- Zero weights are skipped; roughly a quarter of the matrix is connected.
- Integer-only arithmetic; no division in the hot path except the modulo in
  the ambient-stimulus step.
- No heap allocation during update.
- The activation scan and the energy sum are two fixed 24-iteration loops.

These are design budgets, not benchmarks. The HUD reports measured spike
counts and the renderer's FPS at runtime; the headless harness records actual
frame timing in CI. The project does not publish synthetic benchmark numbers
for hardware it has not measured.

---

## 7. What "AI-native" means for the rest of the system

The SNN is the visible part. The architectural commitments behind it are what
will matter for v2.0 and beyond:

### 7.1 Model contracts, not magic

Every neural component in the future runtime must expose: inputs, outputs,
resolution/format, latency bound, memory budget, determinism class, and a
signature. "It's AI" is never an excuse for undefined behaviour.

### 7.2 Capability-gated inference

In v2.0, a process that wants to run a model receives a capability for the
model handle, a memory window, and an NPU device (if present). The kernel does
not sniff data and does not profile users.

### 7.3 Privacy-preserving on-device AI

The long-term thesis: the most private AI is the AI that never sends anything.
ZenithOS is built to make local inference the default:

- no telemetry channel in the default system;
- no network stack until v3.0, and no ambient network capability after;
- model artifacts local, signed, and inspectable;
- activations and traces volatile unless the user explicitly persists them;
- capability model that can deny a process access to sensor-like inputs at the
  kernel level.

### 7.4 Future on-device model runtime (v4.0)

The planned runtime targets small deterministic models, not giant generality:

- int8 or fixed-point activations with integer kernels;
- a stable operator set (dense, convolution, pooling, activation, recurrences)
  specified in `docs/` and versioned;
- a model package format with signature, manifest, memory budget, and
  determinism class;
- an emulated NPU device in QEMU plus a documented register interface for
  hardware co-design;
- strict memory ceilings enforced by the microkernel before loading.

If a model cannot fit the budget or cannot be made deterministic, it does not
qualify for the default system. Users who want larger models can run them in a
server product built on top, at their own privacy cost — not by weakening the
kernel.

---

## 8. Anti-goals

To keep the term honest, ZenithOS explicitly rejects:

- **Cloud-tethered AI.** No API keys, no inference endpoints, no crash
  telemetry in the default system.
- **Opaque models.** No unsigned binary blobs in the kernel image; every model
  in the default tree must be inspectable source or a signed, documented
  artifact.
- **Non-deterministic inference in the trusted path.** If it cannot be
  reproduced, it cannot drive the desktop.
- **AI as marketing veneer.** The network must remain load-bearing: remove it
  and the UI is no longer the same product.
- **Training on user data.** The system does not collect or transmit data,
  therefore it cannot train on it.

---

## 9. Reading the code

| Question | Start here |
| --- | --- |
| What are the neurons and weights? | `src/ai/snn.asm` |
| How do tick and keys become stimulus? | `src/ai/snn.asm`, `src/drivers/pit.asm`, `src/drivers/keyboard.asm` |
| How do activations become pixels? | `src/ui/neural.asm` |
| What does the HUD show? | `src/ui/neural.asm`, `src/lib/log.asm` |
| How is it all verified? | `docs/TESTING.md`, `tools/emulate.mjs` |

---

## 10. Summary

ZenithOS v1.0's AI is a **24-neuron deterministic spiking network in ring 0**
that turns hardware time and keyboard input into the visual state of the
desktop. It is small, auditable, offline, and reproducible — and it is a
starting point, not a finish line. The roadmap moves inference into an
isolated userspace server, adds a signed fixed-point model runtime, and
co-designs the hardware it runs on, while keeping the two properties that
define the project: **the intelligence stays local, and its behaviour stays
inspectable.**

*Next: [`ARCHITECTURE.md`](ARCHITECTURE.md) for the exact implementation,
[`SECURITY.md`](SECURITY.md) for the threat model, and
[`ROADMAP.md`](../ROADMAP.md) for what comes after v1.0.*
