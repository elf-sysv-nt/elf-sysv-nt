# DR-0063 — no image the platform loads carries a %fs-relative thread-pointer access

Status: accepted
Date: 2026-09-01
Deciding: the build worker, on a requirements audit; the compiler-side enforcement
is a codegen decision adjacent to the one AGENTS.md reserves, and is left to the
operator to take.
Proposal: none; taken when an audit found the linker half of this recorded and
enforced but the image-and-compiler half unrecorded.

## What was decided

The thread pointer on this platform is reached through %gs (DR-0003), because a
user-written %fs base does not survive a context switch on this Windows —
spike 1 and `spike/fs-base-fault` measured the loss in the scheduler, no matter
who set the base. It follows that no image the runtime loads may contain a
%fs-relative thread-pointer access: such an instruction reads a segment Windows
has zeroed and returns garbage. This records that as a requirement on every
loadable image, not only as a property the runtime happens to need.

## Why the recorded half is not the whole obligation

WP-12's binutils patch already enforces one half. `ld` relaxes the psABI's TLS
sequences in place and writes `mov %fs:0x0,%rax` itself, so the patch inspects
the relaxation's output and refuses the forms that would presume an fs base —
`GOTTPOFF` and the local-exec relaxation — turning a `__thread` access that would
resolve wrongly into a link error instead. That is the linker half, and it holds.

The compiler half does not. `spike/ld-tls-relaxation` established that the
initial-exec sequence can be emitted directly, and "a linker that relaxes nothing
still passes that instruction through"; its own conclusion is that "the compiler
side needs work regardless." A hand-written asm thread-pointer fetch, or a
prebuilt object, reaches the image the same way. None of these carry a relocation
for the linker patch to catch.

The reason this is worth a record of its own is the failure mode. The granule
constraint (DR-0008) halts loudly at the loader; an unsupported relocation is
refused at link. A bare `%fs:` instruction is neither: there is no relocation to
refuse, and the runtime raises no fault on the access — it silently reads zero.
A requirement whose violation is silent cannot rest on a backstop, because there
is none to build; it has to be met before the image runs.

## Consequences

Enforcement is build-side and has two parts. The compiler for the target must not
emit a %fs thread-pointer sequence — the codegen decision the spike reserved,
left to the operator and carried with the gcc rebuild already tracked (WP-17).
And because the silent case has no runtime catch, a build-side scan of the final
image — `objdump` for a `%fs:`-relative thread-pointer fetch — is the honest
certification that an image is clean, the counterpart to the loader's refusal for
the loud cases.

A prebuilt stock-Linux binary carries psABI %fs TLS in its shipped code and so
cannot be loaded here. This makes explicit, as a requirement, what DR-0000 states
as the floor: the userland is rebuilt from source through this toolchain, not
mapped as vendor binaries. `doc/proposals/0003-vendor-binary-tls-rewriting.md`
records the only escape, and it is unbuilt.

## What it does not decide

The compiler mechanism — a codegen patch, a spec, or a TLS-dialect setting — which
is the operator's to take. Whether the image scan lives in the acceptance harness
or in the package build. And the general-dynamic and dynamic-TLS paths through the
runtime's own `__tls_get_addr`, which are the runtime's concern (DR-0021), not a
property of the image the way an initial-exec `%fs` fetch is.
