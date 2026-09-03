# Toolchain bootstrap stages for ELF-SysV-NT

This fixes the stage numbering for bringing up the GCC-family toolchain for
ELF-SysV-NT, our own target rather than the RHEL 8.10 ELF surface we previously
borrowed. It replaces an earlier three-stage sketch — a single Stage 1 between
the host toolchain and a native one — which collapsed the partial compiler, the
target libc and the full compiler into one step that cannot be taken in one
step. The section below on Stage 1a through 1c is why.

## elfsysv1.dll is fixed to Stage 0

elfsysv1.dll is the ELF-to-PE bridge: it is what the NT loader loads directly,
before any ELF-SysV-NT process can run. Nothing else on NT knows how to load
ELF until elfsysv1.dll exists, so it has to be something NT's native loader
can start on its own — which means PE, not ELF. If it were an ELF binary
itself, something would first have to load it as ELF, and the only thing
capable of that would be elfsysv1.dll. It is PE, and it stays PE, for the life
of the project. Concretely, that means it is built once, by the Cygwin host
toolchain, in Stage 0b below, and never rebuilt by either the cross toolchain
(Stage 1) or the native ELF-SysV-NT toolchain (Stage 2 onward), both of which
only ever emit ELF-SysV-NT output.

## Stage list

| Stage | Produces | Built by |
|---|---|---|
| 0a | Cygwin 3.6.8 host toolchain | (already exists) |
| 0b | Target sysroot skeleton (headers, ABI/syscall tables, crt stubs) and elfsysv1.dll | Stage 0a |
| 1a | Partial cross-compiler (static libgcc only, no threading or shared libs) | Stage 0a, against Stage 0b's sysroot |
| 1b | Target libc body | Stage 1a |
| 1c | Full cross-compiler (shared libs, threading, full libgcc) | Stage 1a rebuilt against Stage 1b |
| 2 | First native toolchain (target = host = ELF-SysV-NT) | Stage 1c, cross-built |
| 3 | Self-hosted rebuild, diffed against Stage 2 for a bit-for-bit match | Stage 2, running under Stage 0b's elfsysv1.dll |
| 4 (optional) | Verification rebuild, confirms Stage 3 reproduces itself | Stage 3 |

Stage 1a through 1c is the part most likely to get collapsed back into a
single "Stage 1" if this document is read too quickly, so it is worth
restating why it is three steps and not one. GCC cannot compile a target C
library without a compiler, and it cannot build a complete compiler — one
with shared-library and threading support — without a target C library
already in place. The partial compiler in 1a exists only to get the libc body
in 1b built; 1c is 1a reconfigured and rebuilt once that dependency is
satisfied. Skipping straight from "cross-compiler" to "libc body" without
naming 1a and 1c invites someone to try building a full-featured cross-gcc
before any target libc exists, which will fail in ways that are irritating to
diagnose from the error message alone.

Stage 4 is marked optional because it is not required for a release build,
only for the first bring-up. Given that this bring-up is also the point where
we stopped borrowing RHEL 8.10's ELF surface and started building our own,
running it at least once is worth the cost: a Stage 3/Stage 4 mismatch is
exactly the failure mode a subtly wrong ABI decision in Stage 0b would
produce, and it is far cheaper to catch here than downstream.
