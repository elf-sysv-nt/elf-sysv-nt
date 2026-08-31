# Test and build environments

Three environments matter to this project, and they do different jobs. DR-0007
names two of them and turns on the distinction between them; the third is the
el8-shaped environment the acceptance differentials need, which the substitutions
ledger (`doc/substitutions.md`) tracks the project onto.

## The pinned certification root

Certification runs on a pinned Cygwin from a 2019 snapshot: version 3.0.7,
gcc 7.4, python 3.6.9, shaped to stand in for RHEL 8.10 — bash 4.4 and python 3.6
match what el8 ships, and the toolchain rejects the newer syntax el8's would.
This is the root `ci/gate.sh` runs on and the only root it will certify from
(DR-0035): a package's exit criterion is met when its suite passes here, not
where some other shell happened to be open. Verifying target code against this
root is what keeps a `str | None` or a `list[str]` from passing review and then
failing at runtime on the real host.

## The runtime base

`elfsysv1.dll` is built from a different Cygwin: version 3.6.10, the
`newlib-cygwin` tree at commit `b11613e47`, which DR-0007 fixes as the runtime
base. Every artifact that reads Cygwin's source for the runtime's own shape — the
export inventory, the down-call wrappers, the veneer version map, and WP-26's
from-source build — takes it from that ref. The 3.0.7 above is the acceptance
floor, not the runtime base; the two are deliberately different versions and the
project depends on not confusing them.

## The WP-T2 el8 differential environment

Several certifications were taken against whatever glibc was locally available
rather than against el8's own — WSL's glibc 2.43 standing in for el8's 2.28 in
the WP-33, WP-35 and WP-40 differentials. That substitution is recorded as row S1
in `doc/substitutions.md`, and WP-T2 is its burn-down.

WP-T2 stands up a pinned el8-shaped userland — a Rocky Linux or AlmaLinux 8.10
install with glibc 2.28, matching the version el8 actually ships rather than a
newer one — and reruns those three differentials against it. A divergence the
newer glibc hid is recorded rather than assumed away; the substitution row closes
when the rerun matches or when its divergence is written down as justified. The
environment is pinned and named here so that "ran against el8" means one specific
userland rather than whatever was to hand.
