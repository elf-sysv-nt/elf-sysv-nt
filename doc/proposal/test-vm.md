# Proposal — a pinned test VM for the runtime-under-test

Status: draft
Author: Philip Dye
Date: 2026-08-31
Analysed against: e862a13 on `march`

Drafted at the operator's request, from a failure this session spent hours
misreading. `elfsysv1.dll` is a Cygwin *runtime* DLL, and the primary host is
the one place it can never be certified cleanly: certifying it there means
loading it into a process that already holds the host's own `cygwin1.dll`, and
two Cygwin runtimes in one process are fragile by construction. This proposes
restoring the test VM the project once had, as the environment where the DLL is
the runtime rather than a guest beside another one.

## What went wrong without it

WP-27's crossing certification loads `a/build/wp27-face/elfsysv1.dll` from a
Cygwin harness and calls its exports. On a quiet machine it passes every time;
under load it fails intermittently, `LoadLibrary` returning error 126 or 998.
Read as a code defect it is maddening, and this session read it as one twice --
once as a duplicate-symbol classification, once as a shared image base -- and
was wrong both times. The real cause is not in the DLL. Both the harness and the
DLL are Cygwin runtimes; both prefer to sit at `0x180040000`; both expect to own
the process's Cygwin state -- its TLS slot, its heap, its signal machinery. One
process cannot give that to two of them, and whether a given load survives
depends on where ASLR happened to put the first one and how much the machine is
thrashing. It is the DLL-rebase fragility `AGENTS.md` already names as the thing
that haunts Cygwin's `fork`, surfacing a work package early.

The host cannot be fixed into a good test environment for this, because the
thing that makes it bad -- an already-loaded `cygwin1.dll` -- is what makes it a
usable development host. The two roles are in tension on the same machine.

## What the VM gives that the host cannot

A clean guest runs `elfsysv1.dll` as *the* Cygwin runtime, with no second one to
coexist with, which is the configuration the DLL is actually built for and the
one production will run. The coexistence fragility disappears because the
coexistence does. Three kinds of test want exactly this and get nothing
trustworthy on the host:

The load and init path. WP-27's `crossing` and `hostload` prove the faced DLL
loads, that `DllMain` and the PE TLS callback fire from the host's own loader,
and that its exports answer System V. On the host these are measured through a
second runtime; in the guest they are measured directly.

The fault and fork paths. WP-22, WP-43 and WP-61 drive faults beneath a System V
frame, signal delivery, and a core written on a crash; the fork work drives the
rebase failure mode head-on. These are destructive by nature, and a guest that
can be reverted to a snapshot after each run is where destructive certification
belongs, not a developer's daily host.

The deployment shape. The loader maps anonymous executable memory, which
`DR-0000` records as permanently malware-shaped to enterprise endpoint
protection. A pinned guest is where that interaction is observed on purpose --
with protection on, as a customer would run it -- rather than discovered by
accident on the build host.

## Where it sits among the environments

`doc/test-environment.md`, as DR-0038 left it, names two environments: the
primary Cygwin root that builds and certifies, and the runtime base as a source
ref. This session added a third in practice, the Rocky 8.10 WSL image that
supplies el8's real `glibc` for the WP-T2 differentials. The test VM is a
fourth, and distinct from all three: not where the code is built, not the glibc
the loader is compared against, but the host the runtime is *run* on with
nothing else competing for the Cygwin state. WP-T4's el8 acceptance -- a vendor
package compiled, linked, and run against `elfsysv1.dll` -- is the same need at
full size, and wants the same guest.

## What this settles and what it leaves open

It settles that the runtime-under-test needs an isolated host, that the host
this project builds on cannot be that, and that the VM which once served the
role should be restored rather than worked around. It records the role in
`doc/test-environment.md` so "certified the load path" names one specific guest
rather than whichever process happened to load the DLL.

It does not settle the mechanics: which hypervisor, how many guests, how they
are provisioned, and how a snapshot is pinned so a rerun means one specific
image are the operator's to decide, the way DR-0038 left provisioning to the
operator. Nor does it change any current verdict: the crossing that passes on a
quiet host is not wrong, only measured in the fragile configuration, and moving
it to the guest is meant to make the pass trustworthy rather than to overturn
it. The one firm claim is that a work package whose done-when is "the runtime
loads and runs" cannot be honestly closed on a host that cannot load it without
a second runtime in the way.

## Not verified

That a single guest suffices for all three kinds. The fault and fork paths may
want a guest configured differently from the load path -- endpoint protection
on for one measurement, off for another -- and whether that is one image with
profiles or several is a provisioning question this draft does not answer.

Whether the lost VM's configuration is recoverable, or whether the pinned image
is built fresh from a named base. Either way the image must be named and
snapshotted, so "ran in the VM" means one specific thing and a rerun reproduces
it.
