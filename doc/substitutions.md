# Substitutions ledger

A certification run against a substitute for the thing it certifies is permitted
and recorded here, per the convention in `AGENTS.md`. Each row names what was
substituted for what, where it happened, and what burns the substitution
down — the rerun against the real target that closes the row. A row closes when
that rerun matches, or when the divergence it finds is written down as
justified. An open row is a known gap, not a hidden one.

## Open

| # | Substitute | For | Where | Burns it down |
|---|------------|-----|-------|---------------|
| S2 | The flags Red Hat's `redhat-hardened-cc1` and `redhat-hardened-ld` specs files inject, spelled out (`-fPIE`, `-pie`) | Running the build under those specs files themselves, through rpm and its macro set | `spike/vendor-hardened-build/`, the build line `acceptance/packages.tsv` carries for bzip2, and `%optflags`/`%build_ldflags` in `toolchain/rpm/macros.elfsysvnt` | An acceptance run against a package built by real rpm macros on a real el8 root, matching the `e_type` and segment placement this substitution claims. Until then the claim is narrow by construction: it covers image shape, which follows from `-fPIE` and `-pie` alone, and nothing about the hardening the other optflags provide. |
| S3 | No annobin annotations | `-specs=redhat-annobin-cc1`, which loads a gcc plugin that stamps build provenance into `.gnu.build.attributes` | `toolchain/rpm/macros.elfsysvnt`'s `%optflags`; the gap is reported every run as `cflags_absent_from_ours` in `spike/vendor-hardened-build/` | Either building the annobin plugin for the cross toolchain and matching the vendor, or a package in the el8 set whose build or check reads the annotations and is shown not to need them. Neither has been done; nothing in this project reads them, which is a statement about this project rather than about el8. |

## Closed

| # | Substitute | For | How it closed |
|---|------------|-----|---------------|
| S1 | WSL glibc 2.43 | el8's glibc 2.28 | Closed 2026-08-31, matched. WP-T2's pinned el8 image stood up as a Rocky Linux 8.10 WSL instance (`rocky8`, `ldd (GNU libc) 2.28`), and the three differentials reran against it through the `LINUX_REF_DISTRO` seam: WP-33's `elf-ldd` load order matched a real `ld.so` on all seven graph cases, WP-35's symbol resolution matched on every collision case, and WP-40's auxv differed from Linux's only in the entries that describe the platform. The transcript is `test/t2-results-2026-08-31.txt`; `test/t2-run.sh` regenerates it. No divergence to justify — the substitute's behaviour held against el8's own. |

## Notes

The substitution in S1 is behavioural, not structural: glibc's observable
resolution, lookup, and auxv semantics are stable across the 2.28-to-2.43 span
for the surfaces these packages certify, which is why the substitute was usable
at all. The burn-down exists because "usable" is a judgement and the rerun is a
measurement, and the project's discipline is to end on the measurement.
