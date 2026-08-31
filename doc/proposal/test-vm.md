# Proposal — a pinned test VM for destructive and deployment-shaped tests

Status: draft
Author: Philip Dye
Date: 2026-08-31
Analysed against: e862a13 on `march`

Drafted at the operator's request, and narrowed after the argument it first
rested on was measured and found false. This session lost hours reading WP-27's
crossing failures as evidence that `elfsysv1.dll` cannot be tested on the host.
It can. On a quiet machine the faced DLL loads and runs every time -- through a
Cygwin harness that also holds `cygwin1.dll` (ten of ten), through a native
loader as the sole runtime (five of five), relocated to a high address in both.
The rebadge does what its comment claims: two differently named Cygwin runtimes
carry different shared-object names and coexist. Every failure was the machine
thrashing under concurrent builds, not a limit of the host. So this proposal
keeps only what a VM is genuinely for, and drops the claim it cannot support.

## What the host does fine, and what it does not

The host tests the runtime's ordinary behaviour without help: load, init,
exports, the crossing, the differentials. What the host is a poor place for is a
narrower set, and the narrowness is the point.

Destructive tests. The fault, signal, core and fork paths (WP-22, WP-43, WP-61,
and the fork work) drive the runtime into states designed to crash or to corrupt
process state on purpose. A test that wedges a thread, leaves a half-forked
child, or trips the rebase failure mode leaves residue on the machine it ran on.
A guest that reverts to a snapshot after each run is where that belongs, so a bad
run costs a rollback rather than a developer's afternoon. This is isolation for
the tester's sake, not the DLL's.

The deployment interaction. The loader maps anonymous executable memory, which
DR-0000 records as permanently malware-shaped to enterprise endpoint protection.
The honest way to measure that interaction is with protection turned on, as a
customer runs it -- which is not something to do on a build host, where a
quarantine action interrupts everything. A pinned guest with endpoint protection
installed is where "how does the deployed shape sit with a real scanner" is
answered on purpose rather than discovered by accident.

The el8 acceptance at size. WP-T4 builds a vendor package against the runtime and
runs its own suite. The WP-T2 differentials this session moved onto a Rocky 8.10
WSL image, which is enough to compare `glibc` behaviour; a full package build and
run against `elfsysv1.dll` as the runtime is a heavier, dirtier job that wants a
revertible environment of its own.

## Where it sits among the environments

`doc/test-environment.md`, as DR-0038 left it, names the primary Cygwin root that
builds and certifies and the runtime base as a source ref; this session added the
Rocky 8.10 WSL image as the el8 `glibc` reference. The VM is not a fourth
certification root -- ordinary certification stays on the host -- but an isolated
run-and-revert environment for the destructive and deployment-shaped tests above.
It is worth naming and pinning so "ran with endpoint protection on" or "ran the
fork stress" means one specific image.

## What this settles and what it leaves open

It settles that the VM's justification is isolation and deployment fidelity, not
an inability to test the runtime on the host -- because that inability does not
exist. It records the narrow role so the proposal cannot be read, as its first
draft could, as a claim the host is unfit.

It does not settle whether the role earns a VM at all: isolation for destructive
tests is real, but a snapshot-revert workflow has a cost, and whether the fault
and fork suites actually corrupt enough host state to need it is a judgement the
operator makes against how often they run and how much they leave behind. Nor
does it settle the mechanics -- hypervisor, image, snapshot discipline -- which
are the operator's as DR-0038 left provisioning to the operator. If the honest
answer is that the host plus care suffices and only the endpoint-protection
measurement truly needs a guest, that is a smaller proposal than this one, and
worth preferring.

## Not verified

Whether the destructive suites leave host residue that actually warrants
isolation, or whether they clean up after themselves well enough that a guest
buys little. This draft asserts the isolation is worth having; it does not prove
the residue is bad enough to require it, and that measurement should precede
standing up a VM rather than follow it.
