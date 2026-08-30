# Decisions

One settlement per file, numbered in the order they were taken, never
renumbered. A record is append-only once filed: reversing one means writing a
new record that points back at it, which keeps the reasoning that was live at
the time from being quietly edited into the reasoning that is live now.

This index is one-to-one with the files beside it. A record without a row here
is a record nobody will find.

| # | Decision | Status | Proposal |
|---|---|---|---|
| [0000](0000-the-cygwin-refaced-floor.md) | The floor is Cygwin re-faced; el8's userland builds unchanged above a glibc-ABI veneer that is not glibc | inception 2026-08-20 | none |
| [0001](0001-target-triple.md) | The target triple is `x86_64-elfsysvnt-linux-gnu` | accepted 2026-08-29 | 0001 |
| [0002](0002-el8-source-acquisition.md) | el8 source comes from Rocky 8.10 and lives outside the repository | accepted 2026-08-29 | 0001 |
| [0003](0003-tls-model.md) | The TLS model is a runtime-owned thread pointer through `%gs`, carrier C3 | accepted 2026-08-29 | 0002 |
| [0004](0004-license.md) | The licence is LGPLv3 or later, inherited from Cygwin's `winsup` | accepted 2026-08-29 | none |
| [0005](0005-bounded-linux-claim.md) | The `linux` field is a bounded claim, not a lie; DR-0001 stands | accepted 2026-08-29 | 0004 |
| [0006](0006-red-zone-direction.md) | The red zone is repaired at the delivery site; `-mno-red-zone` is scaffolding | accepted 2026-08-29 | none |
| [0007](0007-runtime-base-version.md) | The runtime is based on Cygwin 3.6.10 (`newlib-cygwin` b11613e47), not the pinned 3.0.7 | accepted 2026-08-30 | none |
| [0008](0008-mmap-granule-protection.md) | Segment mapping goes through the runtime's `mmap`, one region per object, protection at the host granule; a granule-sharing object is refused | accepted 2026-08-30 | none |
| [0009](0009-down-call-wrapper-convention.md) | The down-call wrapper is a signature-agnostic `ms_abi` tail jump; translation lands at the call site | accepted 2026-08-30 | none |
| [0010](0010-veneer-header-provenance.md) | The veneer's `features.h` is el8's arithmetic, copied not paraphrased | accepted 2026-08-30 | none |
| [0011](0011-ldso-cache-format.md) | the loader's cache is this project's own format, not glibc's | accepted 2026-08-30 | none |
| [0012](0012-host-facing-unwind-seam.md) | host-facing entry points are ms_abi with compiler unwind data; System V frames carry none | accepted 2026-08-30 | none |
| [0013](0013-version-map-companion-sources.md) | the version map's companion set spans glibc, libnsl and libxcrypt | accepted 2026-08-30 | none |
| [0014](0014-at-pagesz-commit-granularity.md) | AT_PAGESZ reports the commit granularity, not the reservation one | accepted 2026-08-30 | none |
| [0015](0015-variadic-rebuild-through-a-core-valist.md) | the variadic veneer rebuilds a Microsoft va_list and repasses through a va_list core | accepted 2026-08-30 | none |
| [0016](0016-relocation-certified-against-vendor-objects.md) | relocation types the platform will not emit are certified against vendor objects | accepted 2026-08-30 | none |
| [0017](0017-version-node-objects-are-scaffold.md) | version-node identity objects are scaffold, not a fourth-bucket stub | accepted 2026-08-30 | none |
| [0018](0018-compatibility-counter.md) | the compatibility counter is Cygwin's, re-faced, enforced on the combined API and kept from the first release | accepted 2026-08-30 | none |
| [0019](0019-symbol-lookup-engine-and-versioning-seam.md) | symbol lookup is a separate engine, and versioning enters through one seam | accepted 2026-08-30 | none |
| [0020](0020-callback-trampoline-no-codegen.md) | callback trampolines are fixed per-shape compiled thunks, one live target per shape, no runtime code generation | accepted 2026-08-30 | none |
| [0021](0021-thread-pointer-carrier-placement.md) | the C3 carrier word is the floor of a runtime-owned stack, not a blind offset below StackBase | accepted 2026-08-30 | none |
| [0022](0022-the-rendezvous-link-map.md) | the rendezvous link map is the SVr4 five-field prefix, found through DT_DEBUG | accepted 2026-08-30 | none |
| [0023](0023-version-match-rule.md) | the version matcher reproduces glibc's observable rule, from the spec | accepted 2026-08-30 | none |
| [0024](0024-static-tls-surplus-and-dtv-shape.md) | the loader's static-TLS surplus and DTV shape, reproduced from the spec | accepted 2026-08-30 | none |
| [0025](0025-init-order-and-the-abi-boundary.md) | initialization order, the cycle tie-break, and calling into a loaded object | accepted 2026-08-30 | none |
| [0026](0026-the-version-script-names-every-symbol.md) | the generated version script names every symbol, not just the nodes | accepted 2026-08-30 | none |
| [0027](0027-the-exec-branch-and-the-interpreter-limit.md) | one classifier for the exec branch, and a four-hop interpreter limit | accepted 2026-08-30 | none |
| [0028](0028-the-low-window-is-reserved-by-the-parent.md) | the low window is reserved by the parent, into a suspended stub | accepted 2026-08-30 | none |
| [0029](0029-what-crosses-the-fork-and-how-it-is-checked.md) | what crosses the fork, and how the child knows | accepted 2026-08-30 | none |

## What earns a record

Anything a different engineer would want the reasoning for six months on,
whichever route the change took. That is a lower bar than it sounds, and it is
deliberately lower than the bar for a proposal: a change can be cheap to undo
and still leave a question behind it worth answering once.

The three reservations in `AGENTS.md` each end in a record by construction.
