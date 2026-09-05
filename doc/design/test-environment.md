# Test and build environments

Three environments matter to this project, and they do different jobs. DR-0038
names the build and certification root, DR-0007 fixes the runtime base as a
source ref, and the third is the el8-shaped environment the acceptance
differentials need, which the substitutions ledger
(`doc/design/substitutions.md`) tracks the project onto.

## The build and certification root

The project builds and certifies in the primary Cygwin root, `C:\-\cygwin\root`:
Cygwin 3.6.10, gcc 14.4, python 3.12, make 4.4. This is the root `ci/gate.sh`
runs on and the only root it will certify from (DR-0038, superseding DR-0035 on
the pin): a package's exit criterion is met when its suite passes here, not
where some other shell happened to be open. The cross toolchain lives at the
root-neutral `C:\-\x-elfsysvnt` and resolves the same from any shell, with a
`~/x-elfsysvnt` symlink covering tests that hardcode the old home-relative path.

An earlier revision of this document pinned certification to the rhel root,
`C:\-\rhel\root` — Cygwin 3.0.7, gcc 7.4, python 3.6.9, a 2019 snapshot shaped
to stand in for RHEL 8.10. DR-0038 retires that root from the certifying role.
It was only ever the el8 emulation DR-0007 names, its gcc cannot build the
3.6.10 runtime, and the audit behind DR-0038 found the project had moved off it
some time before the pin was corrected. It stays on the machine as an emulation
for cross-checking el8's syntax floor — bash 4.4 and python 3.6 match what el8
ships, and its toolchain rejects a `str | None` that would fail on the real
host — but a pass there certifies nothing.

## The runtime base

`elfsysv1.dll` is built from Cygwin source at a fixed ref: version 3.6.10, the
`newlib-cygwin` tree at commit `b11613e47`, which DR-0007 fixes as the runtime
base. Every artifact that reads Cygwin's source for the runtime's own shape — the
export inventory, the down-call wrappers, the veneer version map, and WP-26's
from-source build — takes it from that ref. The base is a source ref, not a
shell. That the certifying root now also runs 3.6.10 is a convenience, not a
collapse of the distinction: the ref pins what the runtime is built *from*, the
root pins where suites *run*, and the project depends on not confusing them.

## The WP-T2 el8 differential environment

Several certifications were taken against whatever glibc was locally available
rather than against el8's own — WSL's glibc 2.43 standing in for el8's 2.28 in
the WP-33, WP-35 and WP-40 differentials. That substitution is recorded as row
S1 in `doc/design/substitutions.md`, and WP-T2 is its burn-down.

WP-T2 stands up a pinned el8-shaped userland — a Rocky Linux or AlmaLinux 8.10
install with glibc 2.28, matching the version el8 actually ships rather than a
newer one — and reruns those three differentials against it. A divergence the
newer glibc hid is recorded rather than assumed away; the substitution row closes
when the rerun matches or when its divergence is written down as justified. The
environment is pinned and named here so that "ran against el8" means one specific
userland rather than whatever was to hand.

## Where the environment is named

Nothing in the tree writes out where the toolchain or the scratch root lives.
`bin/roots.sh` and `bin/roots.py` name three roots -- the checkout, the cross
toolchain's prefix, the el8 scratch -- and every script and both registries
resolve against them, with the values this machine happens to use as the
defaults. A second machine exports what it has; a script that pastes an
absolute path back in fails `bin/check-roots` rather than failing quietly on
somebody else's disk.

The two registries hold the variable, not the value, so `test/t3-regen.sh` and
`bin/check-suites` expand a path before they test it. Expansion substitutes
those three names and nothing more, because a manifest field is data: a typo
should be a path that does not exist, not a command that runs.

Settled by: DR-0096.
