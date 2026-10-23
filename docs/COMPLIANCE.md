# ZenithOS Compliance Roadmap

> **Read this first.** ZenithOS v1.0 is a research and education operating
> system with **no users, no customer data, no network stack, and no
> certification of any kind.** Nothing in this document is a claim of
> compliance. It is a statement of intent: how the project would approach the
> compliance obligations that a future product built on ZenithOS would face,
> and which design choices in v1.0 already point in that direction.
>
> Sections below are tagged:
>
> - **[Implemented]** — true of the v1.0 source today.
> - **[Design intent]** — a principle already reflected in the architecture.
> - **[Planned]** — scheduled in `ROADMAP.md`, not started.
> - **[Aspirational, no date]** — a direction, explicitly not a promise.

---

## 1. Why compliance matters to an operating system

An OS sits underneath everything: it mediates memory, devices, identity, and
time. Whatever trust a user places in an application is ultimately borrowed
from the kernel. Compliance frameworks — GDPR, accessibility law, security
certifications, export controls — are how organisations formalise that trust
for customers and regulators.

ZenithOS v1.0 does not need to be compliant: it processes no personal data and
is distributed as MIT source. But the architecture should not make future
compliance impossible. The goal of this document is to name the obligations
early so that the microkernel and POSIX milestones are designed with them in
mind, rather than retrofitted later.

---

## 2. Current v1.0 posture

| Dimension | v1.0 reality |
| --- | --- |
| Personal data processed | None. The OS collects no telemetry, has no network driver, and stores nothing persistently. |
| Data at rest | No filesystem, no disk writes, no persistent storage. |
| Data in transit | No network stack. |
| Accounts / identity | None. |
| Third-party code in image | None. The image is assembled only from project-authored NASM. |
| Licence | MIT for source and docs. |
| Accessibility | Small screen, keyboard-only interaction; no accessibility requirements met or claimed. |
| Export classification | Publicly available, open-source cryptographic *code: none* (no crypto in v1.0). |
| Security certification | None, and none claimed. |

**[Implemented]** The unified logger writes to COM1 and an in-memory ring
buffer. Both are volatile; the ring buffer is destroyed on power loss. No frame
is written to persistent storage. **[Implemented]** The build consumes only
project sources and NASM; there are no npm dependencies in the OS build
(`tools/emulate.mjs` is a dev-only harness).

---

## 3. GDPR and privacy-by-design

The GDPR applies to processing personal data. A kernel that never touches
personal data is largely out of scope — but the moment a future release adds
networking, storage, or an AI assistant that observes users, it is in scope.
The project therefore adopts **data minimisation by design** as an
architectural rule, not a checkbox.

### 3.1 Principles already reflected

- **[Design intent] Local-only intelligence.** The SNN runs in-kernel and
  consumes nothing but ticks and key transitions. There is no model upload, no
  inference endpoint, no telemetry stream. This is the technical basis of a
  privacy-preserving on-device AI claim.
- **[Design intent] Volatile state by default.** v1.0 has no persistence
  layer. When persistence arrives (v3.0), it will be opt-in at the API level.
- **[Design intent] No ambient authority (v2.0).** Capability-based access
  means a future application can only read what it was explicitly granted,
  which is the microkernel expression of data minimisation.
- **[Planned, v1.1] Observable data flows.** The logger's stable event IDs make
  it possible to audit which subsystem touches which data.

### 3.2 Commitments for future releases

| Obligation | Approach | Milestone |
| --- | --- | --- |
| Lawful basis & transparency | Any future telemetry must be documented, off by default, and configurable locally | v3.0+ |
| Right to erasure | Persistent stores must support per-object deletion; encryption keys per object so erasure is cryptographic | v5.0 |
| Data protection by design | Capability-gated access, minimised retention, no derived profiles in kernel | v2.0+ |
| Records of processing | `docs/privacy/` data-flow register published per release | v3.0 |
| Data subject requests | Documented procedure once a product exists | v5.0 |
| Breach notification | Security policy contact and advisory process (see `SECURITY.md`) | v1.1 |
| International transfers | Not applicable while local-only; reassessed with networking | v3.0 |

**[Aspirational, no date]** A Consumer Edition with a signed, audited privacy
statement and a reproducible-data-flow attestation.

---

## 4. Security certifications (aspirational)

None of the following is held, audited, or claimed. They are recorded because
companies that adopt an OS in regulated environments eventually ask for them,
and the architecture should be shaped to make an audit feasible.

| Framework | What it would require | v1.0 gap | Earliest plausible milestone |
| --- | --- | --- | --- |
| **SOC 2 Type II** | Documented controls over 6+ months, independent audit, evidence automation, incident response | No controls programme, no org, no customers | v5.0 |
| **ISO/IEC 27001** | ISMS, risk register, asset inventory, supplier management, continual improvement | No ISMS; risk register exists only for engineering | v5.0 |
| **IEC 62443** (industrial) | Security levels for embedded controllers, secure boot, patch management, zone/conduit model | No secure boot, no signed images, no patch story | v4.0–v5.0 |
| **FIPS 140-3** | Validated cryptographic module | No crypto in v1.0 | Not planned |
| **Common Criteria** | Protection profile, formal evaluation | Not feasible for a hobby-scale project | Not planned |

**[Planned, v4.0]** Signed images and measured boot are prerequisites for any
serious certification conversation; they are on the roadmap independently of
compliance deadlines because they are good security engineering.

**[Aspirational, no date]** If a product organisation is ever formed, the
compliance function will report to the foundation board and publish audit
summaries. Certifications will only be claimed with a certificate number and
scope on file.

---

## 5. Accessibility

Accessibility is a legal requirement in many jurisdictions (for example the
European Accessibility Act and Section 508 in the United States) and, more
importantly, a design quality. v1.0 is a 320×200, 256-colour, keyboard-only
demo; it makes **no accessibility claims**.

- **[Implemented]** Keyboard-only operation; no pointer required.
- **[Implemented]** HUD text is rendered by a high-contrast 5×7 bitmap font on
  a dark background, with palette-defined colours rather than ad-hoc constants.
- **[Design intent]** The palette lives in `ui/theme.asm`, so a high-contrast
  or colour-blind-safe theme is a data change, not a rendering rewrite.
- **[Planned, v4.0]** Accessibility mode: large-text HUD, reduced-motion
  camera, high-contrast palette, optional slower animation rate, and an
  accessible command palette.
- **[Planned, v4.0]** Document the accessibility conformance target for any
  shipped product (WCAG 2.2 AA for documentation; platform-specific guidance
  for the UI).
- **[Aspirational, no date]** Independent accessibility audit and a public
  statement of conformance for a product release.

The roadmap evaluates accessibility as part of UX acceptance at v4.0, not as
an afterthought.

---

## 6. Licence hygiene and open-source provenance

- **[Implemented]** Project source and documentation are MIT.
- **[Implemented]** The default build has no third-party dependencies. NASM is
  used as a build tool and is not linked or shipped in the image.
- **[Implemented]** `.gitignore` excludes the host NASM distribution files, so
  the repository tree contains only project sources.
- **[Planned, v1.1]** `THIRD_PARTY.md` and an SPDX header convention for any
  future vendored code; an SBOM (SPDX JSON) produced by CI for every release.
- **[Planned, v3.0]** Package metadata enforces a licence field; `.zpkg`
  manifest includes licence and provenance; the repository indexes licences.
- **[Planned, v5.0]** Automated licence scanning in CI and a documented
  review process for any non-MIT dependency.

**Compatibility rule:** the default image must remain free of proprietary or
copyleft-incompatible components. A driver or library with a different licence
may be offered as an optional package, never as a default component.

---

## 7. Export controls and sanctions

- **[Implemented]** v1.0 contains no cryptographic software. Nothing in the
  image performs encryption, authentication, or key exchange.
- **[Implemented]** The project publishes publicly available source code,
  which is generally exempt from export-control classification in most
  jurisdictions (for example the "publicly available" provisions of the U.S.
  EAR and the EU dual-use regulation), but that assessment is a legal question
  the project does not purport to settle.
- **[Planned]** If cryptography is added (image signing at v2.0+, TLS at
  v3.0+), the project will document the algorithms and key lengths, and will
  not knowingly add restricted cryptography without legal review.
- **[Planned]** Contributors must not submit code subject to sanctions-
  restricted access; the project will comply with applicable sanctions in
  distributing binaries and accepting contributions.
- **Note:** this section is informational, not legal advice. Anyone shipping a
  product must obtain their own counsel.

---

## 8. Ethical use and dual-use

An AI-native OS is a dual-use technology. The project commits to:

- publishing safety-relevant limitations (no isolation, no privilege
  separation in v1.0) prominently in `README.md` and `SECURITY.md`;
- refusing to describe the system as suitable for safety-critical, medical,
  automotive, or industrial control use until the corresponding milestones are
  complete and certified;
- keeping the AI model's behaviour inspectable: deterministic, local, and
  documented in [`AI-NATIVE.md`](AI-NATIVE.md);
- adding a public "intended use" statement before any binary distribution.

---

## 9. Reporting compliance concerns

Open an issue with the `compliance` label for anything in this document that
is inaccurate, misleading, or inconsistent with the source. For legal,
licensing, or security-sensitive matters, use the private channel described in
[`SECURITY.md`](SECURITY.md). The project would rather correct an overclaim
than defend one.

---

## 10. One-page summary

| Area | Today | Next concrete step |
| --- | --- | --- |
| Privacy | No data collected or stored; local-only SNN | Audit data flows when persistence lands (v3.0) |
| Security certs | None claimed | Signed images + measured boot (v4.0) |
| Accessibility | Keyboard-only, themeable palette | Accessibility mode + audit target (v4.0) |
| Licence | MIT, no third-party code | SBOM + SPDX in CI (v1.1) |
| Export | No crypto | Legal review before adding crypto (v2.0+) |
| Governance | Bootstrap maintainer | Foundation + TSC (v5.0) |

*Compliance is a direction, not a badge. This page will grow teeth only when
there is a product and an organisation behind it.*
