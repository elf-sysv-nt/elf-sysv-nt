# Decisions

One settlement per file, numbered in the order they were taken, never
renumbered. A record is append-only once filed: reversing one means writing a
new record that points back at it, which keeps the reasoning that was live at
the time from being quietly edited into the reasoning that is live now.

This index is one-to-one with the files beside it. A record without a row here
is a record nobody will find.

Supersession lives in the Status cell, since that is where status already
lives. When a record replaces another outright, the replaced record's cell
gains `; superseded by NNNN` after whatever it held, so
`accepted 2026-08-30; superseded by 0038` is the shape, and the replacing
record's own header says `Supersedes: DR-NNNN`. `bin/check-design-links` reads
that phrase and nothing else from the cell: a superseded record stops being
required in a governing document's Settled-by line, and citing one becomes a
failure.

Partial supersession takes no marker. DR-0035 is the case in hand — DR-0038
pinned the gate to the primary root and left the rest of it standing — so its
cell is unmarked, both records are in force, and `Verification-Plan.md` cites
the pair and says which reading is current.

| # | Decision | Status | Proposal |
|---|---|---|---|
| [0001](0001-target-triple.md) | The target triple is `x86_64-elfsysvnt-linux-gnu` | accepted 2026-08-29 | 0001 |
| [0002](0002-el8-source-acquisition.md) | el8 source comes from Rocky 8.10 and lives outside the repository | accepted 2026-08-29 | 0001 |
| [0003](0003-tls-model.md) | The TLS model is a runtime-owned thread pointer through `%gs`, carrier C3 | accepted 2026-08-29; superseded by 0101 | 0002 |
| [0005](0005-bounded-linux-claim.md) | The `linux` field is a bounded claim, not a lie; DR-0001 stands | accepted 2026-08-29 | 0004 |
| [0006](0006-red-zone-direction.md) | The red zone is repaired at the delivery site; `-mno-red-zone` is scaffolding | accepted 2026-08-29; superseded by 0050 | none |
| [0007](0007-runtime-base-version.md) | The runtime is based on Cygwin 3.6.10 (`newlib-cygwin` b11613e47), not the pinned 3.0.7 | accepted 2026-08-30; superseded by 0097 | none |
| [0008](0008-mmap-granule-protection.md) | Segment mapping goes through the runtime's `mmap`, one region per object, protection at the host granule; a granule-sharing object is refused | accepted 2026-08-30; superseded by 0100 | none |
| [0009](0009-down-call-wrapper-convention.md) | The down-call wrapper is a signature-agnostic `ms_abi` tail jump; translation lands at the call site | accepted 2026-08-30; superseded by 0097 | none |
| [0010](0010-veneer-header-provenance.md) | The veneer's `features.h` is el8's arithmetic, copied not paraphrased | accepted 2026-08-30; superseded by 0097 | none |
| [0011](0011-ldso-cache-format.md) | the loader's cache is this project's own format, not glibc's | accepted 2026-08-30; superseded by 0103 | none |
| [0012](0012-host-facing-unwind-seam.md) | host-facing entry points are ms_abi with compiler unwind data; System V frames carry none | accepted 2026-08-30; superseded by 0097 | none |
| [0014](0014-at-pagesz-commit-granularity.md) | AT_PAGESZ reports the commit granularity, not the reservation one | accepted 2026-08-30 | none |
| [0015](0015-variadic-rebuild-through-a-core-valist.md) | the variadic veneer rebuilds a Microsoft va_list and repasses through a va_list core | accepted 2026-08-30; superseded by 0097 | none |
| [0016](0016-relocation-certified-against-vendor-objects.md) | relocation types the platform will not emit are certified against vendor objects | accepted 2026-08-30; superseded by 0103 | none |
| [0018](0018-compatibility-counter.md) | the compatibility counter is Cygwin's, re-faced, enforced on the combined API and kept from the first release | accepted 2026-08-30; superseded by 0097 | none |
| [0020](0020-callback-trampoline-no-codegen.md) | callback trampolines are fixed per-shape compiled thunks, one live target per shape, no runtime code generation | accepted 2026-08-30; superseded by 0097 | none |
| [0021](0021-thread-pointer-carrier-placement.md) | the C3 carrier word is the floor of a runtime-owned stack, not a blind offset below StackBase | accepted 2026-08-30; superseded by 0101 | none |
| [0022](0022-the-rendezvous-link-map.md) | the rendezvous link map is the SVr4 five-field prefix, found through DT_DEBUG | accepted 2026-08-30; superseded by 0103 | none |
| [0024](0024-static-tls-surplus-and-dtv-shape.md) | the loader's static-TLS surplus and DTV shape, reproduced from the spec | accepted 2026-08-30 | none |
| [0025](0025-init-order-and-the-abi-boundary.md) | initialization order, the cycle tie-break, and calling into a loaded object | accepted 2026-08-30; superseded by 0097 | none |
| [0027](0027-the-exec-branch-and-the-interpreter-limit.md) | one classifier for the exec branch, and a four-hop interpreter limit | accepted 2026-08-30 | none |
| [0028](0028-the-low-window-is-reserved-by-the-parent.md) | the low window is reserved by the parent, into a suspended stub | accepted 2026-08-30; superseded by 0100 | none |
| [0029](0029-what-crosses-the-fork-and-how-it-is-checked.md) | what crosses the fork, and how the child knows | accepted 2026-08-30 | none |
| [0030](0030-the-shape-of-a-signal-delivery.md) | the receiving thread builds the signal frame, and the return is an iretq | accepted 2026-08-30 | none |
| [0031](0031-status-lives-in-a-tracked-ledger.md) | build status is a tracked ledger and the worker is driven from the plan | accepted 2026-08-30 | none |
| [0033](0033-an-elf-core-from-the-runtime.md) | a fatal signal leaves an ELF core, written by the runtime | accepted 2026-08-30 | none |
| [0034](0034-the-manifest-is-the-installers-memory.md) | the installer's only memory is a manifest under the root | accepted 2026-08-30; superseded by 0097 | none |
| [0035](0035-ci-is-the-pre-merge-gate.md) | CI is a pre-merge gate on the pinned root, not a hosted service | accepted 2026-08-30 | none |
| [0036](0036-ratification-sweep-0008-0035.md) | ratification sweep: DR-0008 through DR-0035 pass the decision ladder and are ratified | accepted 2026-08-30 | proposal F7 |
| [0038](0038-the-build-environment-is-the-primary-root.md) | the build and certification environment is the primary Cygwin root (3.6.10); supersedes DR-0035 on the CI root | accepted 2026-08-30 | none |
| [0039](0039-one-trunk-sessions-land-from-worktrees.md) | one merge-only trunk; every session lands from its own worktree via session-start/session-land | accepted 2026-08-30 | proposal 0005 |
| [0040](0040-atomic-dll-install.md) | the faced DLL is installed by rename, not by copy | accepted 2026-08-31; superseded by 0097 | none |
| [0050](0050-retire-mno-red-zone.md) | `-mno-red-zone` is retired; the red zone is honored at the delivery site | accepted 2026-08-31 | none |
| [0059](0059-run-init-chain-before-entry.md) | the loader runs a crossed image's DT_INIT chain before entry, across the ABI boundary | accepted 2026-09-01; superseded by 0097 | none |
| [0061](0061-images-are-linked-granule-separable.md) | every image the platform loads is linked granule-separable | accepted 2026-09-01 | none |
| [0062](0062-cet-opt-out-is-a-toolchain-default.md) | CET opt-out belongs in the toolchain default, not only the rpm macros | accepted 2026-09-01 | none |
| [0063](0063-images-carry-no-fs-relative-tls.md) | no image the platform loads carries a %fs-relative thread-pointer access | accepted 2026-09-01 | none |
| [0064](0064-programs-get-granule-not-page-protection-precision.md) | a program's own protection changes land at the granule, not the page | accepted 2026-09-01; superseded by 0100 | none |
| [0070](0070-the-ladder-measures-before-it-escalates.md) | the decision ladder measures before it escalates | accepted 2026-09-01 | none |
| [0073](0073-a-weak-undefined-is-not-a-demand.md) | a weak undefined symbol is not a demand on the runtime | provisional 2026-09-02; superseded by 0103 | none |
| [0074](0074-lifts-are-cleared-by-text-and-practice.md) | a lift is cleared by licence text and recorded practice; LGPL-2.1-or-later is open | provisional 2026-09-02 | none |
| [0075](0075-governing-documents-cite-their-records.md) | the governing documents carry the citations, and a checker holds them | accepted 2026-09-02 | 0006 |
| [0076](0076-architecture-is-sized-by-coherence.md) | the architecture document is sized by coherence, not by a word ceiling | accepted 2026-09-02 | none |
| [0077](0077-window-reconcile-is-plain-pe-only.md) | the window reconcile of DR-0068 and DR-0069 is live for the plain-PE shape only | accepted 2026-09-03; superseded by 0100 | none |
| [0078](0078-doc-splits-by-what-a-file-claims.md) | doc/ splits by what a file claims about the present: design, history, and the plans between them | accepted 2026-09-03 | none |
| [0084](0084-every-check-is-designated-where-it-is-written.md) | every check carries a written designation in test/suites.tsv, and the gate tier matches ci/suites.txt | accepted 2026-09-03 | none |
| [0095](0095-the-licence-is-chosen-not-inherited.md) | the licence is LGPL-2.1-or-later, chosen here rather than inherited from what the veneer derived from | accepted 2026-09-05 | 0011 |
| [0096](0096-paths-resolve-from-named-roots.md) | scripts and registries resolve three named roots instead of writing a machine's paths out longhand | accepted 2026-09-05 | none |
| [0097](0097-proposal-0011-is-ratified.md) | proposal 0011 is ratified, and every record whose subject is the veneer arc retires with it | accepted 2026-09-05 | 0011 |
| [0098](0098-proposal-0012-is-ratified.md) | proposal 0012 is ratified: two seams under the core, one kernel process under H, the interface's object, the I/O rule | accepted 2026-09-05 | 0012 |
| [0099](0099-vfork-and-clone-vm-per-substrate.md) | `vfork` and `CLONE_VM \| CLONE_VFORK` share the parent's memory under H and are a fork under N | accepted 2026-09-05 | 0012 |
| [0100](0100-the-veneer-address-space-records-retire.md) | the veneer's address-space records (DR-0008, 0028, 0064, 0077) retire; `AT_PAGESZ` is rehomed | accepted 2026-09-05 | 0012 |
| [0101](0101-the-thread-pointer-under-n-is-tlsslots-63.md) | under N the thread pointer is `TlsSlots[63]`, reserved through the PEB bitmap (carrier C1); DR-0003 and DR-0021 superseded | accepted 2026-09-06 | 0012 |
| [0102](0102-criteria-3-and-4-amended-and-both-substrates-offered.md) | criteria 3 (WSL only) and 4 (per substrate) amended; both substrates offered, the client decides; the seam gets its teeth | accepted 2026-09-06 | 0012 |

## What earns a record

Anything a different engineer would want the reasoning for six months on,
whichever route the change took. That is a lower bar than it sounds, and it is
deliberately lower than the bar for a proposal: a change can be cheap to undo
and still leave a question behind it worth answering once.

The three reservations in `AGENTS.md` each end in a record by construction.
| [0103](0103-proposal-0013-is-ratified-and-the-loader-records-retire.md) | proposal 0013 is ratified: the governed set states the kernel, glibc's `ld.so` is the loader under both substrates, the loader records retire, `.note.elfsysvnt.abi` carries the gate ABI version | accepted 2026-09-06 | 0013 |
| [0104](0104-acceptance-is-a-count-per-substrate.md) | acceptance is a count per substrate, rebuilt under N and shipped under H, reported side by side | accepted 2026-09-06 | 0013 |
| [0105](0105-the-conformance-classes-are-defined-at-the-syscall-boundary.md) | the conformance classes are defined at the syscall boundary | accepted 2026-09-06 | 0013 |
| [0106](0106-the-canary-and-the-pointer-guard-take-the-two-slots-below-the-thread-pointer.md) | the canary is `TlsSlots[62]` and the pointer guard `TlsSlots[61]`, below the thread pointer; the PEB reservation is three bits; DR-0101 amended | accepted 2026-09-06 | 0013 |
