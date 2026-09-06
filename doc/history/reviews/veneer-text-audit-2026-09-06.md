# Audit: veneer-era text in the four unread governing documents

*2026-09-06. Read against 0011, 0012, DR-0097 to DR-0102 and the
Architecture rewrite of 2026-09-05. Asked for by the operator's
`veneer-text-audit` decision of 2026-09-05, with the recommendation taken:
audit first, one list per document, then the rewrites as one proposal. This
is the list. It changes no document.*

The rule applied is the one the Architecture rewrite used. Every sentence
that states a mechanism is read for its subject and marked **N** (true of
the native substrate only), **H** (true of the hypervisor substrate only),
**both**, or **veneer** (its subject is the `elfsysv1.dll` bootstrap that
0011 replaced and DR-0097 retired; no present-tense reading exists). A
sentence that names no substrate but is true of both is not a finding.
Sentences that state a promise rather than a mechanism (scope, non-goals,
the acceptance bar) are marked the same way, and it is those that make the
rewrite a proposal rather than a fold-back: DR-0097 retired the records that
settled them, and nothing has yet settled their replacements.

Summary first. `doc/ROADMAP.md` is veneer from its first line to its last
and should be retired to history, not rewritten; 0011 § 18 and 0012's
phases are the roadmap. `Requirements.md` is veneer in every section except
the last paragraph of Acceptance; its three promises (scope, non-goals, the
acceptance count) are each contradicted by 0011 and need restating.
`Verification-Plan.md` is veneer in four of its nine sections, current in
three, and mixed in two; the kernel's criteria section added on 2026-09-06
is the seed the rewrite grows from. `target-definition.md` is mostly
**both** and mostly right; its errors are one section (the limit of the
`linux` claim), one paragraph (the loader SONAME as a veneer), and a
scattering of `WP-` and `DR-0028` citations to retired work.

## doc/ROADMAP.md

Homed records: none carry a Settled-by line; the document cites DR-0001,
DR-0003, DR-0005, DR-0006 in prose. `check-design-links` does not govern
it.

| Where | Text | Mark | Note |
|---|---|---|---|
| Opening | "before an el8 userland builds against this tree" | veneer | The premise is a rebuilt userland linking `elfsysv1.dll`. Under N a userland is rebuilt against the gate; under H nothing is rebuilt. |
| The path assumed, table | Runtime face: `elfsysv1.dll`, "System V outward over an MS-ABI core", spike 3 | veneer | There is no runtime face; the boundary is the syscall (0011 § 1). |
| The path assumed, table | TLS model: DR-0003, carrier C3, `_my_tls` | N, wrong | DR-0101 superseded DR-0003: carrier C1, `TlsSlots[63]`. Under H the thread pointer is `%fs` as the ABI says. |
| The path assumed, table | Target triple row | both | Current; DR-0005's bound is inverted by 0011 (see target-definition below). |
| Red zone paragraph | "Cygwin's signal delivery takes `%rsp-8` first ... `-mno-red-zone` policy below is unchanged" | veneer | DR-0050 retired `-mno-red-zone`; delivery is the kernel's (spikes 38, 47). |
| Order of construction | "toolchain -> runtime -> TLS -> loader -> versioning -> process image -> exec -> fork -> signals -> veneer -> debugging -> packaging" | veneer | 0011 § 18's order is substrate, core phases 1 to 4, the seam, H's leaf. |
| § 1 toolchain | the target definition and the cross toolchain | N | Right in substance for N; every `WP-` citation is to retired work. Under H there is no toolchain in the path (Architecture § Toolchain and images). |
| § 2 the runtime `elfsysv1.dll` | whole section | veneer | Nothing to salvage. |
| § 3 thread-local storage | `%gs` carrier, `_my_tls` shape | N, wrong | DR-0101. The TCB layout paragraphs are glibc's and true under both. |
| § 4 the dynamic loader | the loader as `elfsysv1.dll`'s body faced as `ld-linux-x86-64.so.2` | veneer | Under N the platform's loader (DR-0011, 0016, 0022, 0027, 0073 stand); under H el8's own `ld.so` (Architecture § The loader). |
| § 5 symbol versioning | the version-node face of `libc.so.6` | veneer | Under N glibc is rebuilt and carries its own nodes; under H it is shipped. No platform-owned version map exists. |
| § 6 initial process image | auxv built by the runtime, low window reserved by the parent stub | veneer | DR-0100 retired DR-0028; the kernel builds the auxv (0011 § 4). |
| § 7 exec dispatch and the PE host stub | whole section | veneer | There is no PE stub per image; `exec` is the kernel's (0011 § 5). |
| § 8 fork | Cygwin's fork replaying mappings | veneer | Fork is the NT clone under N (spike 35) and a page-table copy under H (spike 44); DR-0099 for `vfork`. |
| § 9 signals | delivery through Cygwin's path | veneer | Delivery is the kernel's under both (0011 § 7, spikes 38, 47). |
| § 10 the libc veneer | whole section | veneer | |
| § 11 debugging | link map rendezvous | both | Right; DR-0022 stands. Rest of the section assumes the DLL. |
| § 12 packaging and the rpm surface | rpm against `elfsysv1.dll`'s provides | N, partly | Under N a rebuilt userland is packaged; the provides are glibc's own. Under H el8's RPMs are installed as shipped (0011 § 16). |
| § 13 test infrastructure | the face certifications, the exec bar | veneer | `test/suites.tsv` and the twelve criteria replace it. |
| Not verified | every item | veneer | All six concern the DLL, Cygwin's fork, or the stub. |

Disposition: retire whole to a `veneer-roadmap` file under `doc/history/` with a header
saying what it was, as `veneer-address-space.md` was retired by DR-0100.
Replace with a page-length `doc/ROADMAP.md` that points at 0011 § 18 for the
order, 0012 § 10 for what the seam adds, `doc/milestones.md` for the spikes,
and `test/suites.tsv` for the bar, and states the three decisions it once
tabulated as settled (DR-0001, DR-0101, 0011 § 1).

## doc/design/Requirements.md

Homed records: none (no Settled-by line, and `check-design-links` lists it as
governed, which means every record whose subject it states is expected to
be cited there; DR-0097's retirements left it citing nothing).

| Section | Text | Mark | Note |
|---|---|---|---|
| Preamble | "the gABI ... the psABI ... glibc's own documentation for symbol versioning" as the external specifications | veneer, partly | The external specification is now the Linux kernel ABI (0011 § 1: the constants are Linux's by construction) plus the gABI and psABI; glibc's versioning is the userland's business. |
| Scope ¶1 | "presents ELF and the System V AMD64 ABI upward and Windows NT downward, so that el8's userland builds against it" | veneer | The scope is a Linux kernel personality with two substrates; el8's userland builds against it under N and is shipped under H (0011 goals, DR-0102). |
| Scope ¶2 | "resolves against a `libc.so.6` that answers by glibc's names at glibc's version nodes" | veneer | The `libc.so.6` is glibc's own, rebuilt (N) or shipped (H). |
| Scope ¶3 | "Nothing in that path asks the Windows loader to understand ELF" | both | True and worth keeping. |
| Scope ¶4 | "whatever the runtime beneath the face happens to use" | veneer | There is no face; the values are Linux's (0011 § 1). |
| Non-goals ¶1 | "Not a Linux kernel. The kernel ABI is satisfied by rebuilding against `elfsysv1.dll`, not by dispatching system calls" | veneer, inverted | It is a Linux kernel personality that dispatches system calls (0011 title). 0011's non-goals replace this list: no security boundary, no init system, no network namespaces or containers. |
| Non-goals ¶2 | "Not a Cygwin replacement. The floor is Cygwin re-faced" | veneer | DR-0000 replaced outright by 0011; DR-0097. |
| Non-goals ¶3 | "Not a binary compatibility layer. Nothing here promises to run a binary built on a real Linux" | veneer, inverted for H | Under H that is the promise (0011 § 16, DR-0102). Under N the rebuild stands. |
| Non-goals ¶4 | "one runtime version, one el8" | N | True of N's rebuilt userland; H has no runtime version. The "pinned pair" survives as one el8. |
| Conformance classes | A/B/C defined on "the face", `veneer/libc/` tables | veneer in wording, both in idea | The classes are sound for a kernel boundary: A is the syscall ABI's constants and struct layouts (Linux's by construction, so class A is a check of the headers the rebuild uses and the kernel's own tables); B is behaviour against the Rocky 8 oracle, which is what every criterion is; C is a recorded divergence, of which the tree now has real ones (spike 39 q9: truncate under a mapped view refused, directory rename over an open child refused). `veneer/libc/` does not exist. |
| Acceptance ¶1 | "at least <N> rebuild from vendor source against this platform, run, and pass their own test suites" | N | Right for N and still unset. Under H the acceptance suite is the distribution's own installed and run (0011 § 16, criterion 5 onward); the count under H is packages that run as shipped. |
| Acceptance ¶2–4 | export face, bare `ret` bodies, `acceptance/accept.sh`, `acceptance/packages.tsv`, DR-0082, `rhelcyg-8.10` | veneer | `acceptance/` carries over to N "unchanged in purpose" (0011 cross-cutting), but the harness it describes credits symbol bodies that no longer exist. |
| Acceptance ¶5–8 | the demand census: 1898 / 625 / 516, 62.3%, 52.1% | veneer, superseded in kind | The census measured reach against a floor with missing interfaces. Under a kernel the bound is different: what N cannot rebuild (raw `syscall`, Go) and what H cannot run (nothing in principle). Spike 51's census is the replacement measurement and is running. |
| Acceptance ¶9–11 | DR-0079's claimed surface, cheap and expensive counters, `veneer/wiring/bodies.tsv` | veneer | Retired with DR-0079. |
| Acceptance ¶12 | "how many packages call `syscall` ... the instruction is the interface under H and is never reached under N" | both | Written 2026-09-05; current. Spike 51 is the count. |
| Not verified | the number, the 2893 denominator, the three classes partitioning "the face", package tests as proof | mixed | The first two survive; the third is veneer; the fourth is both. |

Disposition: rewrite in place under one proposal, keeping the document's
shape (scope, non-goals, classes, acceptance, not verified) and its length,
with scope and non-goals taken from 0011's goals section, the classes
redefined at the syscall boundary, and acceptance stated per substrate with
the count still the operator's blank.

## doc/design/Verification-Plan.md

Homed records: DR-0035, DR-0038 (§ The gate), DR-0102 (§ The kernel's
criteria).

| Section | Text | Mark | Note |
|---|---|---|---|
| Preamble | "the design-gaps review found" | history | Fine as history; the sentence about what the document is for stands. |
| The floor a differential runs against | "compares this platform against a real glibc ... el8's 2.28 on the RHEL root ... `LINUX_REF_DISTRO`" | both, wrong object | The oracle is now el8's kernel behaviour as seen through the `rocky8` WSL instance (Core-Phase1.md, criterion 1), not glibc 2.28 on a Cygwin root. The rule (a claim is proved against the oracle or carries a substitution row) stands; the object changes. S4 in substitutions.md already records the WSL kernel standing in for 4.18. |
| The bar per class | "`veneer/include/` is vendored byte-identical" | veneer | The class A bar becomes: the kernel's constant and struct tables are generated from or checked against el8's kernel headers (0011 § 1). Class B's examples (resolution order, initializer order, auxv, search path) are N-loader examples and stand for N; under H they are el8's `ld.so`'s and not tested here. Class C stands. |
| The gate | `ci/gate.sh`, `pre-merge-commit`, "certifies on the primary Cygwin root, the 3.6.10 installation that Architecture § Floor and derivation describes" | mixed | The gate mechanism stands (DR-0035). "Architecture § Floor and derivation" no longer exists; DR-0038's build environment "remains the build environment" (0011 cross-cutting) as a host, not as a floor. The 3.0.7 sentence is veneer. |
| What the registry must carry | "the exec bar, the realproc layer, the face crossings ... the exec suite joins ... the face certifications do not join" | veneer | `test/suites.tsv` with the twelve criteria and the substrate conformance suite is the registry now (DR-0102). |
| Substitution | the rule; S2 (hardened flags), S3 (annobin) | both, N | The rule stands. S2 and S3 are N-toolchain substitutions and stand for N. |
| Acceptance | `ready`, forwards, filled stubs, shims, `veneer/wiring/bodies.tsv`, DR-0052, DR-0057, DR-0085 | veneer | Retired with DR-0097. The acceptance section restates per substrate what Requirements says. |
| The kernel's criteria | | both | Current (DR-0102). Its last paragraph is the promise this audit discharges: "where they and 0011's criteria disagree, the criteria are current." |
| Fuzz and unit obligations | loader, relocator, verdef/verneed matcher | N, both | The kernel's ELF mapper and N's loader parse attacker-shaped input under both; the versioning matcher is N-only. "Build leaf to trunk" stands. |
| The spike contract | | both | Current. |
| Not verified | "constant table in the wiring layer", "the differential floor ... `rocky8`", "fault path ... DR-0042" | mixed | First is veneer; second stands and is truer than ever; third is veneer (DR-0042 retired). |

Disposition: rewrite in place under the same proposal; four sections
replaced (bar per class, registry, acceptance, not verified), two amended
(floor, gate), three kept.

## doc/design/target-definition.md

Homed records: DR-0001, DR-0005 (prose citation), and by subject DR-0061,
DR-0062. The table of six values is current and every value is unchanged by
0011: the triple, `EI_OSABI`, the ABI-tag, the loader SONAME, `uname`, the
PIE default are all things el8's userland expects and both substrates
supply.

| Section | Text | Mark | Note |
|---|---|---|---|
| The triple | sysroot, tool prefix, rpm target | N | Right for N's toolchain; under H there is no toolchain and the triple is a name only. Say so in one sentence. |
| The limit of the `linux` claim | "One item on that list is not delivered. A toolchain reading `linux` assumes a `syscall` instruction reaches a kernel, and here it does not" | veneer, inverted | 0011 inverts DR-0005: under H the instruction is the interface; under N it is never reached because the rebuilt userland calls the gate. Architecture § Target and claim already says this. The section's second half (why the field is not a lie, the vendor-binary TLS problem, "the syscall half is not yet anybody's work package") is veneer: 0011 § 4.3 and § 16 are that work. |
| EI_OSABI | rule and loader obligation; WP-12, WP-31 | both | Right; the WP citations point at retired work packages. Under H the loader that checks the byte is el8's. |
| The .note.ABI-tag | "Our loader reads it in vendor binaries"; WP-12, WP-14 | both | Right in substance: N's crt emits it; H's userland carries el8's. WP citations retired. |
| The dynamic linker SONAME | "it is a veneer in the same sense `libc.so.6` is: the loader's body lives in `elfsysv1.dll`, and this object is the ELF-shaped face of it. WP-53 builds both the same way." | veneer | Under N `ld-linux-x86-64.so.2` is glibc's own `ld.so` rebuilt against the gate (0011 § 5 and § 16), or the platform's loader per Architecture § The loader; the two documents disagree today and the rewrite must settle it (finding 4 below). Under H it is el8's. The first two paragraphs (forced by `PT_INTERP`, the file must exist) stand. |
| What uname reports | "the Linux kernel ABI, satisfied by rebuild rather than by syscall dispatch"; "We do not have a 4.18 kernel. We have a runtime that answers the questions"; "a raw `syscall` instruction has no runtime to reach"; WP-25 API counter | veneer, inverted | The table is right. The prose bound is 0011's opposite: there is a kernel, with 4.18's ABI, and a program that reaches an unimplemented call gets `ENOSYS` from it (0011 § 1). The release string stays `4.18.0-elfsysvnt`. |
| The PIE default | WP-13, WP-41, DR-0028 parent-side reservation | both, citations retired | The default stands (N's toolchain; el8's own under H). DR-0028 is retired by DR-0100; the non-PIE image is the kernel's mapper's job under both. |
| Where the name actually lives | `.note.elfsysvnt.abi` carrying WP-25's API major and minor | veneer, partly | The carrier can stay; what it carries was the DLL's compatibility counter (DR-0018, retired). Under H nothing emits it. Either drop the note or redefine the payload as N's gate ABI version; the proposal must pick. |
| Done when | `bin/check-target-definition` | both | Stands. |
| Not verified | `ELFSYSVNT` note owner; the release string against real packages; non-PIE handled by DR-0028 | mixed | Third is retired. |

Disposition: amend in place under the same proposal: rewrite two sections
(the limit of the `linux` claim, what uname reports' prose), one paragraph
(the loader SONAME), settle the `.note.elfsysvnt.abi` payload, and strip
the `WP-` and DR-0028 citations. The six values and the table do not
change, which is the point of the document.

## What the rewrite has to settle, and cannot settle by itself

Three of the findings are promises without a record behind them since
DR-0097, and a rewrite that stated them would be settling them by
editorial act:

1. The scope and non-goals of the platform as stated to a reader who has
   not read 0011. 0011's goals section is the source; a record should say
   Requirements.md restates it.
2. The acceptance bar per substrate: the rebuilt count under N (the
   operator's blank, unchanged) and what counts under H (packages installed
   from el8's RPMs whose own test suites pass unmodified). 0011's criteria
   are the kernel's bar; the package-level bar has no record.
3. The conformance classes redefined at the syscall boundary, and whether
   class A is "checked against el8's kernel headers" or "generated from
   them".

And two mechanisms the documents disagree on today, which the rewrite
must not paper over:

4. Which loader runs under N. Architecture § The loader says the
   platform's own, with its own cache format (DR-0011, kept by DR-0097
   as describing a thing "the new design still has, in different
   clothes"); 0011 § 5 says "the kernel is not a dynamic loader and has
   none; `ld.so` is glibc's, unmodified", and § 16 rebuilds glibc, `ld.so`
   included, against the gate. One of them is the design, and the
   difference is a loader's worth of code.
5. Whether `.note.elfsysvnt.abi` survives, and what it carries.

These five are the proposal's decision log. Everything else in the tables
above is a fold-back to text the records already settle.
