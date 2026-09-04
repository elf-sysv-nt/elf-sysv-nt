# WHP as a substrate

Can this machine run a Linux thread inside a Windows Hypervisor Platform
partition — long mode, ring three, a `syscall` that comes back to the host, a
page fault the host can see — and what does one exit cost?

The answer is yes on every count, and the cost is a few microseconds.
`results-2026-09-04.txt` is the transcript; `finding=whp-usable`.

## Why it matters

Proposal 0011 designs two substrates under one core and does not choose between
them. Its **open question 1** makes the choice mechanical:

> If a syscall under H costs under 10 µs and the operator accepts the
> hypervisor feature as a prerequisite, H is the better kernel emulation by
> every measure but syscall latency, and it removes the toolchain and rebuild
> projects entirely; if either condition fails, N.

Section 3 of that proposal prices a WHP exit at five to fifteen microseconds,
and its **Not verified** section is honest about where the figure came from:
the timings quoted there are "in order, a target, a recollection of published
gVisor and WHP measurements, and a measurement from `spike/gs-thread-pointer/`.
Only the last is this project's." This spike turns the middle one into a
measurement of this project's own, on the operator's machine. Phase 0's spike
(f) is what section 18 asks for, and this is it.

The second condition — whether the operator accepts the hypervisor feature as
a prerequisite — is not a measurement and this spike does not touch it.

**Gates.** Open question 1, and through it the substrate choice that phases 1
onward are built against.

## Running it

    ./measure.sh -o results-$(date +%F).txt

Nothing is installed and no privilege is wanted, which is the point: whether
the Windows Hypervisor Platform optional feature is on cannot be asked of DISM
without elevation, but `WHvGetCapability(WHvCapabilityCodeHypervisorPresent)`
answers it from an ordinary user process. If the answer had been no, the
finding would have been `hypervisor-absent` or, one step further in,
`whp-feature-disabled`; turning the feature on is an elevated, reboot-requiring
operator action, outside what a spike may do.

`-n N` sets how many exits are timed per mechanism, 20000 by default. A full
run is a few seconds. Take the timing run on an idle host: the median is stable
under load, the ninety-ninth percentile is not.

## Method

`whp-probe.c` builds a guest by hand rather than assembling one. Four
megabytes of host memory become guest physical memory at GPA 0 through
`WHvMapGpaRange`, and into it go identity page tables covering the first
sixteen megabytes with two-megabyte pages, a GDT laid out where `STAR` wants
it (ring-0 code at 0x08, ring-3 data at 0x23, ring-3 long code at 0x2b), a
64-bit TSS, and a few dozen bytes of machine code. One
`WHvSetVirtualProcessorRegisters` call puts the processor in long mode at CPL
3: WHP takes segment registers as cached descriptors, so entering ring three
needs no `iret` and no bootstrap in the guest at all.

There is deliberately no IDT. Anything that faults where the spike did not
intend it triple-faults, and the host sees
`WHvRunVpExitReasonUnrecoverableException` — a loud answer rather than a
handler quietly absorbing a mistake. That choice found the one real surprise
here: `hlt` is privileged, so the first ring-3 sequence, written to halt when
it was done, triple-faulted instead. Ring three leaves through a memory access
the partition cannot satisfy, or through `syscall`. Nothing else.

The syscall path is the mechanism the whole substrate rests on, and it is four
bytes of guest code. `IA32_EFER.SCE` is set, `LSTAR` points at a shim that
executes `hlt` and then `sysretq`. A ring-3 `syscall` therefore arrives at the
host as `WHvRunVpExitReasonX64Halt` with `%rcx` carrying the user return
address the instruction pushed there and CS switched to the ring-0 selector
`STAR` names; the next `WHvRunVirtualProcessor` call runs the `sysretq` that
sends the guest back. Because the guest jumps back to its own `syscall`, that
whole path loops, and one run call is exactly one syscall round trip.

Two latencies come out of this. The first is the cheapest exit the partition
can be made to take repeatedly, a ring-0 `hlt` with a jump back to it, which
prices the exit alone. The second is the ring-3 syscall round trip above,
which is what open question 1 is actually asking about. That they land within
noise of each other is the useful part: the syscall path costs what an exit
costs, and the shim adds nothing measurable.

For the comparison baseline, `NtQuerySystemTime` turns out to price a memory
load rather than a ring transition — modern Windows serves it from the shared
user data page — so the probe also times `NtQueryInformationProcess`, which
has to trap. Both are in the transcript. Per-call `QueryPerformanceCounter`
readings are coarser than either, so the native numbers are median batch
means over 500-call batches; the WHP numbers are per-exit, where a 100 ns
counter tick is a rounding error against an exit of several microseconds.

## What this does not reach

The guest here is a few dozen bytes with no interrupt handling, no APIC, no
second virtual processor, and no `WHvEmulatorTryIoEmulation` behind the memory
exits it takes. Section 3's ring-0 shim is a few hundred instructions that
owns an IDT and spills register state to a per-vCPU page; this proves the shape
of the mechanism, not that the shim is writable in that budget.

Nothing multi-threaded is measured. One vCPU on one NT thread is the easy
case, and substrate H wants one vCPU per Linux thread, all of them exiting into
the same host process. Whether the exit cost holds when eight vCPUs are exiting
at once — whether the number here is per-exit or per-host — is unmeasured and
is the first thing to measure next.

The exit measured is a halt, chosen because it repeats without host
bookkeeping. A real syscall exit under H does more: read registers out, write
registers back, and touch guest memory. Those calls are not in this number.
Nor is any kernel work. Read the figure as the floor a syscall under H sits
on, not as what one will cost.

`as_clone`, the fork primitive, is untouched. So is `thread_interrupt` —
`WHvCancelRunVirtualProcessor` exists and this spike never calls it, and
whether asynchronous interruption of a running vCPU costs what a normal exit
costs matters to signals under H.

The measurement was taken on an idle host, on one machine, one AMD processor,
one Windows build. It says nothing about Intel, about a machine with other
hypervisor-rooted features contending, or about what happens under a nested
hypervisor.

## Asking the same question of a machine you do not own

`whp-corp-probe.ps1` is the portable half of this spike. The probe above needs
a Cygwin root, a mingw cross-compiler and a machine you can build on; a managed
corporate laptop has none of those, and the question it has to answer is not
the same question anyway. Here the question was what an exit costs. There the
question is whether the substrate is permitted at all, which on a managed
machine is decided by policy long before it is decided by hardware.

Run it as:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\whp-corp-probe.ps1

It is read-only and unelevated: no registry write, no feature install, no
service start, nothing written outside the transcript. That is deliberate, and
it is what makes the script safe to hand to whoever administers the machine —
which is the likely next step, since the common verdicts are requests rather
than answers.

It reports the policy surface (Windows edition and build, whether the machine
is itself a virtual desktop, whether the Microsoft hypervisor is running,
VBS/HVCI, code-integrity enforcement, AppLocker, the endpoint product) and then
takes one live measurement: bind `WinHvPlatform.dll`, ask
`WHvGetCapability(HypervisorPresent)`, and, if it says yes, create a partition,
set it up, create a virtual processor and map guest memory. That is enough to
prove the substrate functions. It deliberately does not run guest code or time
an exit — that needs the compiled probe beside it, and the answer to "how fast"
only matters once "is it allowed" is yes.

The binding is `DefinePInvokeMethod` rather than `Add-Type`, because
`Add-Type` needs a C# compiler and writes a temporary DLL, and a fleet strict
enough to be worth probing is a fleet where both may be refused. Constrained
Language Mode refuses the reflection either way; the script detects that,
reports `blocked-cannot-measure`, and keeps the policy surface it already
gathered. Two `whp-` findings that read alike are kept apart on purpose:
`not-measured-by-request` is `-NoLiveTest`, and only `blocked-cannot-measure`
says something about the fleet.

The verdicts are `whp-usable`, `whp-partial`, `whp-feature-disabled`,
`hypervisor-not-running`, `nested-virt-unavailable`, `edition-unsupported`,
`host-below-floor`, `blocked-cannot-measure`, `not-measured-by-request`. Exit
status is 0 for usable, 1 for not, 3 for could-not-measure.

It is not in `test/spike-regen.tsv`, and that is the point rather than an
oversight. A regen row asserts that a rerun here reproduces a recorded finding;
this script's findings are *supposed* to differ per machine, and the one thing
it would certify on this host — that WHP works — is what the compiled probe
already certifies. Registering it would tie the suite to a fact it does not
own.

Verified on this host on 2026-09-04: `finding=whp-usable`, agreeing with the
compiled probe, and stable across reruns. Two probe defects were found and
fixed in the writing, both of which would have read as corporate refusals and
were not: `Marshal::AllocHGlobal` is not page-aligned, so `WHvMapGpaRange`
returned `E_INVALIDARG` on a perfectly healthy machine until the allocation
moved to `VirtualAlloc`; and `wsl.exe -l -v` writes UTF-16, so the WSL2 check
reported "no" on a host that has a distro. The Constrained Language Mode path
was exercised in a deliberately constrained runspace rather than reasoned
about.

### The Python twin

`whp-corp-probe.py` asks the same questions with the same verdict words and
prints the same transcript shape, and it exists for one case the PowerShell
version cannot reach.

Constrained Language Mode is the lockdown a strict fleet actually applies, and
it is more surgical than it sounds: cmdlets keep working under it, so the
PowerShell probe keeps its whole policy surface, and what it loses is
`DefinePInvokeMethod` — the one thing standing between it and the measurement.
That is the worst possible trade, because it fails exactly on the machine you
most wanted an answer from. Python is not subject to CLM, and a `.py` file is
not one of the script classes WDAC's script enforcement covers, so `ctypes`
reaches `WinHvPlatform.dll` where reflection is refused. Where PowerShell is
unrestricted the two agree and either will do; where it is restricted, use the
Python one.

It shells out to PowerShell for the handful of fields only WMI knows (VBS,
HVCI, code-integrity status, the endpoint product), which is safe to do here
precisely because those cmdlets survive CLM. When PowerShell is missing or
refuses, those fields read `unknown` and the live test is unaffected — the
split is deliberate, so that a restricted policy surface never costs the
measurement.

Run it as:

    py -3 whp-corp-probe.py

Verified against the PowerShell twin on this host on 2026-09-04: both report
`finding=whp-usable`, and after two fixes they agree on every shared key.
Both fixes were in the Python side and both are worth naming, because each
would have put a false line in a corporate transcript. `ProductName` under
`CurrentVersion` still reads "Windows 10 Pro" on Windows 11 — Microsoft never
revised the value, and WMI's `Caption` is where the true name lives — so the
build number corrects it at the 22000 boundary. And PowerShell hands back
.NET's `True`/`False` where the twin's vocabulary is `yes`/`no`, which left
`hypervisor_running` reading differently in two transcripts describing one
machine.

### Redaction, and why it fails closed

A diagnostic transcript is a disclosure, and this project has a live example of
that going wrong. A `gh` auth diagnostic in the sibling `logs` repo prints a
section headed "full environment (redacted)" and redacts nothing: it carries an
Azure DevOps personal access token and a set of database passwords in
cleartext, in a file whose entire purpose is to be sent to somebody. The label
was the only defence and the label was false.

The Python probe therefore scrubs on the way out. Every emitted value and every
line of generated prose passes through `scrub_value` / `scrub_text` before it
reaches a transcript, rather than the collector being trusted not to have
picked anything up. That ordering is the point: this probe reads named fields
and never enumerates the environment, so in principle it cannot carry a secret,
and "in principle" is exactly what the gh diagnostic had too.

Two rules do the work. Field names that look like credentials are dropped
whole. Values that look like secrets are dropped whatever the field is called —
GitHub tokens, AWS key ids, Slack tokens, JWTs, PEM private-key headers,
`Password=` inside a connection string, and a catch-all for long mixed-case
alphanumeric runs, which is the rule that catches an Azure DevOps PAT sitting
in a variable nobody thought to look at.

The part worth copying is that it fails closed. A canary carrying one instance
of each shape runs through the scrubber before anything is collected; if any of
it survives, the probe prints the failure and exits 3 rather than writing a
transcript that claims a redaction it did not perform. The result is reported
in the transcript itself as `scrub_self_test`, beside a `redactions` count, so
the claim is checkable by the person receiving the file rather than taken on
trust.

Verified 2026-09-04 against the real thing: fed the actual `gh-auth.txt`, the
scrubber catches both the Azure DevOps PAT and the inline DSN password, and
fires only its `high-entropy` and `inline-password` rules doing it. Fed the
probe's own field values — an HRESULT, a BIOS model string, a Windows path, a
module name, a verdict word, and a 64-character SHA-256 digest — it changes
none of them, the digest surviving because the entropy rule demands upper,
lower and digit together and a hex digest is single-case. Sabotaging both rule
sets makes `main` refuse with exit 3, which is the behaviour that matters and
the one the gh diagnostic did not have.

### What a Citrix desktop adds, and what it takes away

The Python probe carries an environment section the PowerShell twin does not,
because a virtual desktop moves the question. It reads the ICA session name,
the Citrix registry, the installation type, Developer Mode, whatever is
injected into the probe's own process, and the volume flags of the directory
you would build in.

Three of those bear directly on findings phase 0 recorded, and each is a place
where a phase 0 verdict does not carry:

Spike (f)'s answer does not survive virtualisation on its own terms. WHP in a
guest needs nested virtualisation from whatever runs the farm, it is off by
default on Citrix Hypervisor, ESXi, Hyper-V and the usual cloud SKUs alike, and
turning it on is a change to the farm rather than to the desktop. The probe
separates the two Citrix shapes for exactly this reason: a Citrix registry on
physical hardware is Remote PC Access — a real workstation reached over ICA —
and the nested-virt objection does not apply to it at all.

Spike (b)'s answer is bounded to Windows Defender, and a Citrix VDA hooks user
sessions. The ntdll-only host process is the one shape that cannot tolerate an
injected module importing `kernel32`, so `foreign_modules` is the list to
re-measure that finding against. The scan reports what is loaded into the
probe's own ordinary process, on the reasoning that whatever reaches this one
reaches that one.

Spike (e)'s answer was measured on local NTFS. Its on-disk format puts uid,
gid, mode and device nodes in extended attributes and symlinks in reparse
points, and a redirected profile or mapped home drive supports neither — which
on a Citrix desktop is very often the directory you would actually build in.
`fs_carries_lx_metadata` answers it from the volume flags without writing a
byte, and `--path` points it at the drive you mean rather than the one you
happen to be standing in.

Developer Mode is worth reading precisely so it is not overread. It grants
unprivileged symlink creation, which is real: spike (e) needed a privilege for
LX symlinks and this supplies it. It grants nothing toward the hypervisor —
enabling an optional Windows feature still wants an administrator and a reboot.

Honesty about coverage: everything else in this README was measured here, and
the Citrix branch was not, because this machine is not a Citrix desktop. The
registry paths, the ICA session name and the installation type are written from
documentation, and the first real Citrix transcript should be read with that in
mind. What *was* verified here is the machinery underneath: the module scanner
reports nothing on a clean process and reports `cygwin1.dll` once one is loaded
from outside the system directories, and the volume flags read `NTFS`, EAs and
reparse points on a local disk. The detection works; which branch a Citrix box
takes is unmeasured.

One reading worth carrying into the decision, which the script says out loud
when it applies: wherever WSL2 is permitted, the hypervisor is already on, and
substrate H is available for the same reason WSL2 is — so the case for building
it has to be made against WSL2 rather than against its absence. Where WSL2 is
forbidden, the feature it rests on is usually forbidden with it, and substrate
H goes with it. The environment where H is the answer is the narrow one that
permits the hypervisor and not the Linux userland on top. Substrate N is the
one that survives a locked-down fleet, and that asymmetry is worth knowing
before either is built.
