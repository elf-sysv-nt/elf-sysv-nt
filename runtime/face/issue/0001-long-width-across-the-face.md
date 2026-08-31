# 0001 — `long` is 64 bits to the face and 32 in the body behind it

Raised 2026-08-31, from a crossing-test failure that turned out to name a real
gap rather than a test artifact.

## What was seen

The sole-runtime crossing harness, built native (mingw, LLP64), read a garbage
result from `labs(-5)` where the old Cygwin-built harness read 5. The immediate
cause was in the test: it typed `labs_fn` as `long`, which is 32 bits on
Windows and 64 in the System V ABI the face presents, so it passed a 32-bit
value into a 64-bit register slot whose upper half was undefined. Cygwin-gcc
happened to sign-extend it, mingw-gcc did not. The test is fixed to `int64_t`.

But the fix to the test exposes a fact about the face that is not a test
concern.

## The gap

`elfsysv1.dll` is compiled with the Cygwin toolchain, which is LLP64: its own
`long` is 32 bits. It presents the System V / Linux ABI, which is LP64: a `long`
there is 64 bits. So for any faced function whose signature involves `long`, an
el8 caller passes or expects 64 bits and the Microsoft-ABI body behind the face
reads or returns 32. The `sv2ms` translation moves the argument into the right
register; it cannot supply the high 32 bits a 32-bit-`long` body never computes.

`labs(-5)` survives because −5 fits in 32 bits. The functions that will not:

- `ftell` / `ftello` return `long`: 64-bit file offsets on Linux, truncated to
  32 in the Cygwin body, so a position past 2 GB comes back wrong.
- `strtol` clamps to `LONG_MAX`: ±2⁶³ on Linux, ±2³¹ in the body, so a value in
  between is clamped where el8 would not clamp, and `LONG_MAX` itself differs.
- `sysconf`, `labs` at large magnitude, and anything returning a `long` count,
  offset, or limit are the same shape: correct for small values, silently wrong
  past 2³¹.

## Why it is not a build or `./configure` problem

Real el8 software is compiled with the target toolchain
(`x86_64-elfsysvnt-linux-gnu`), which is LP64. Its `long` is 64 bits, `./configure`
detects LP64 the way it does on any Linux, and caller and face agree. The
mismatch is only ever between a *Windows-compiled* unit and the face, which is
the test's situation and not the product's. Nothing in a header or a configure
probe is needed on the el8 side.

The divergence is Windows-versus-Unix: every 64-bit Unix GNU target — Linux,
GNU/Hurd, the BSDs, Darwin, Solaris — is LP64, and Windows alone is LLP64. This
project straddles that one boundary because it is a Windows host presenting a
Unix ABI, so the split lands inside the face rather than between two hosts.

## What is owed

A classification pass over the faced surface for `long` in a signature —
argument or return — and a decision per function: whether the Cygwin body's
32-bit `long` can represent the value range el8 expects, or whether the face
must widen it (compute or extend to 64 bits) rather than route straight through.
This belongs with the `sv2ms` convention (DR-0009) and is exactly the same-name,
different-meaning divergence the differential tests and WP-T2 exist to catch; a
differential over `ftell` past 2 GB or `strtol` near `LONG_MAX` against a real
el8 `glibc` would surface it. Recorded here so it is decided rather than found
later by a truncated offset.
