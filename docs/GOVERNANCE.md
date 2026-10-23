# ZenithOS Governance

> Status: **v1.0 — bootstrap governance.** ZenithOS is currently maintained by
> its founding maintainer with a public, documented process for contributions.
> The foundation and elected technical steering committee described in
> [`ROADMAP.md`](../ROADMAP.md#v50--foundation-governance-compliance) are
> planned for v5.0 and do not exist today. This document describes the process
> that *is* in force, and the process the project commits to adopting as it
> grows.

---

## 1. Mission and scope

ZenithOS exists to explore a specific thesis:

> An operating system can be small enough to understand, reliable enough to
> trust, fast enough to feel instant, and structured around a deterministic
> on-device intelligence — without becoming a general-purpose platform.

The project is governed in service of that thesis. Governance decisions are
judged by four questions:

1. Does it keep the system comprehensible? (Can a new contributor read the
   core in a week?)
2. Does it improve or preserve reliability? (Does it weaken any boot gate?)
3. Does it respect the AI-native contract? (Determinism, locality, no
   telemetry.)
4. Does it keep the door open to the roadmap (POSIX, microkernel), or lock us
   into a dead end?

Scope boundaries are enforced in [`ROADMAP.md`](../ROADMAP.md#non-goals).

---

## 2. Roles

### 2.1 Users

Anyone who builds, boots, embeds, or studies ZenithOS. Users participate by
filing issues, reporting security problems, testing release images, and
writing documentation. No CLA is required; contributions are accepted under
the MIT licence.

### 2.2 Contributors

Anyone with a merged pull request. Contributors are expected to follow the
[Code of Conduct](#9-code-of-conduct) and the contribution workflow below.

### 2.3 Reviewers

Contributors who have demonstrated sustained, high-quality work in one
subsystem (boot, arch, drivers, mm, AI, UI, docs, build/CI) and who are
nominated by a maintainer. Reviewers can approve pull requests in their
subsystem; a maintainer still merges.

### 2.4 Maintainers

Maintainers own subsystems, hold merge rights, and are listed in
`.github/MAINTAINERS.md` once that file exists; until then the list is the
release changelog plus the repository collaborators. Maintainers are
responsible for:

- triaging issues and RFCs in their subsystem;
- running the release checklist;
- enforcing the boot gates and documentation accuracy;
- mentoring reviewers.

### 2.5 Lead maintainer

The lead maintainer holds the casting vote during the bootstrap period, owns
the roadmap, and is the designated security contact until a security team is
formed. This is a temporary concentration of authority, explicitly scheduled
for dissolution at v5.0.

### 2.6 Technical Steering Committee (planned, v5.0)

An elected body of 5–7 members, at least two of whom are not employed by the
same organisation, with published meeting minutes and a public voting record.
The TSC will own the roadmap, the release train, and disputes that cannot be
resolved inside a subsystem.

---

## 3. Decision making

ZenithOS uses **lazy consensus with explicit escalation**.

| Decision class | Examples | Mechanism |
| --- | --- | --- |
| Trivial | docs typo, comment fix, test rename | One maintainer approves |
| Normal | driver bug fix, new test, refactor with no ABI change | Lazy consensus: 72 h review window, one subsystem reviewer + one maintainer |
| Significant | new module, HUD redesign, build-pipeline change | Pull request + subsystem reviewer + maintainer; changelog entry |
| Architectural | ABI change, memory model, kernel/userspace split, new third-party code | RFC accepted *before* implementation; prototype may be required |
| Roadmap / governance | release scope, new maintainer, licence change | Public RFC + 14-day comment period + lead maintainer (TSC from v5.0) |

**Consensus rules.** A decision proceeds when no maintainer has an unresolved
objection and at least one maintainer has approved. Objections must be
technical and specific ("this breaks reproducible builds because X", not "I
dislike it"). The lead maintainer may call a vote; the vote is public and
recorded in the RFC thread.

**Escalation.** Disagreements are first resolved between the parties, then by
the subsystem maintainer, then by the lead maintainer/TSC. No private
side-channels: escalate in the open issue.

**Reversibility.** Prefer reversible decisions. A merge behind a build flag is
cheaper than a revert war.

---

## 4. RFC process

The RFC process is how architectural change happens. It is deliberately light
while the project is small.

1. **Idea** — open a GitHub Discussion or issue with the `idea` label.
2. **Draft** — copy `docs/rfcs/0000-template.md` (created when the first RFC
   lands) into `docs/rfcs/NNNN-title.md`; include motivation, design, ABI
   impact, alternatives, risks, and migration.
3. **Review** — a 14-day public comment period; at least two maintainers must
   review; the build/CI and documentation impact must be stated.
4. **Decision** — accepted, rejected, or deferred, recorded in the RFC header
   (`status: accepted | rejected | deferred`) and in the changelog.
5. **Implementation** — pull request(s) referencing the RFC.
6. **Archive** — accepted RFCs are immutable; superseding RFCs link backwards.

RFCs are required for: syscall ABI tables, GDT/IDT/segment layout changes,
paging and memory models, capability semantics, IPC format, build-artifact
format, licence additions, and anything that changes the boot path.

---

## 5. Maintainer ladder

```text
user → contributor → reviewer → maintainer → lead maintainer / TSC
```

- **Reviewer nomination:** a maintainer nominates; a second maintainer
  seconds; public 7-day comment; no sustained objection.
- **Maintainer nomination:** a reviewer is nominated after sustained
  contributions (rough guide: 20+ quality pull requests in a subsystem or
  equivalent architectural work); two maintainers must approve; public 7-day
  comment period.
- **Inactivity:** a maintainer inactive for 6 months is moved to emeritus
  status with thanks; emeritus maintainers may return by request.
- **Removal for cause:** violation of the Code of Conduct or repeated bad-faith
  technical behaviour, decided publicly by maintainers with a documented
  rationale.

---

## 6. Pull request requirements

Every pull request must:

1. Pass CI: the required `build.yml` build job (assembly + image validation),
   and review the advisory headless v86 visual job's screenshots and serial
   log. The visual job becomes a required gate in v1.1.
2. Keep the build deterministic: no timestamps embedded in the image, no
   host-specific paths in artifacts.
3. Include tests for behaviour changes; for UI changes, include or update a
   screenshot from `tools/emulate.mjs`.
4. Update documentation in the same change when behaviour changes (README,
   `docs/ARCHITECTURE.md`, `docs/TESTING.md`; `docs/SECURITY.md` for security
   changes).
5. Respect the write boundaries of the project (no vendored blobs, no build
   products).
6. Be small and reviewable. Large mechanical changes are split by subsystem.

Commit messages follow Conventional Commits (`feat:`, `fix:`, `docs:`,
`test:`, `build:`, `chore:`) and reference the relevant issue or RFC.

---

## 7. Release management

- `main` is always buildable and always boots.
- The lead maintainer (or a designated release manager) cuts releases using
  the checklist in [`ROADMAP.md`](../ROADMAP.md#release-engineering).
- A release is tagged only after: clean CI, reproducible SHA-256 on two hosts,
  self-test report captured, and screenshots committed.
- Security fixes may be released out of band from `main` or a maintenance
  branch.
- Release notes state both what is new and what is still missing; no
  unqualified marketing claims.

---

## 8. Security policy

The security process is defined in [`SECURITY.md`](SECURITY.md). In summary:

- Private disclosure via GitHub Security Advisories (preferred) or the
  contact address published there.
- Acknowledgement within 72 hours, triage within 7 days, fix or mitigation
  target within 90 days.
- No demand for NDA; credit offered unless the reporter prefers anonymity.
- Security fixes take priority over feature work and can bypass the normal
  review window (but not the boot gates).

---

## 9. Code of Conduct

All participants must follow the Contributor Covenant v2.1 (adopted by
reference; the full text will be vendored in `CODE_OF_CONDUCT.md` when the
community programme lands). In short:

- Be respectful and specific. Critique ideas and code, not people.
- No harassment, discrimination, sexualised content, or doxxing.
- Assume good faith; ask before escalating.
- Maintainers enforce with warnings, temporary bans, or permanent bans, and
  document the action privately with the accused and publicly in aggregate.
- Report concerns to the lead maintainer (or the future Code of Conduct
  committee). Retaliation is itself a violation.

---

## 10. Communication channels

| Channel | Purpose |
| --- | --- |
| GitHub Issues | Bugs, feature requests, RFC drafts |
| GitHub Discussions | Ideas, Q&A, design conversations |
| Pull requests | Code and documentation review |
| `docs/` | Durable decisions (RFCs, architecture, security, governance) |
| Release changelog | User-facing changes |

There is no private mailing list and no paid support tier today. If a private
channel ever exists, its existence and members will be published here.

---

## 11. Licensing and intellectual property

- Source is MIT. Documentation is MIT. The boot image contains only code
  authored for the project.
- Contributions are accepted under the contributor's own copyright, licensed
  MIT. No copyright assignment is required.
- Every third-party component must be recorded with its licence and upstream
  commit in `THIRD_PARTY.md` before merge; the default image must remain
  proprietary-blob-free.
- The project name and logo are not covered by the source licence; usage
  guidelines will be published with the foundation.

---

## 12. Amendments

This document changes by public pull request with a 7-day comment window and
approval from the lead maintainer (TSC from v5.0). Every change is recorded in
the git history and summarised in the release notes.

---

## 13. Bootstrap-period honesty

Until v5.0 lands, this is a **benevolent-dictator-with-a-public-process**
model, not a foundation. That is appropriate for a small project and
inappropriate for a long-lived platform. The roadmap commits to changing it,
and this section will be deleted — with a changelog entry — on the day the
foundation charter is published.

---

*Last reviewed: v1.0 release.*
