# DR-0051 — the jmp_buf shims take a frameless face, not a call-style wrapper

Accepted 2026-08-31. Source: WP-56, the runtime slice's shim worklist.

## Context

The runtime slice's live-crossing increment left five rows unattempted:
`_setjmp`, `setjmp`, `_longjmp`, `longjmp`, `siglongjmp`. The increment right
before this one measured why they are shims rather than thunks: el8's
`jmp_buf` is 64 bytes (`__jmp_buf_tag`, eight mangled longs) and Cygwin's is
256 bytes (`_JBLEN` 32 on `__x86_64__`), so a shim cannot translate the
buffer in place the way the stat family or `sigaction`'s mask do -- it needs
an out-of-line real Cygwin buffer the el8-shaped one leads back to, and that
increment scoped out where that buffer lives and how a caller finds it again
as open questions, deliberately not attempting an implementation.

That framing treated the open problem as buffer identity alone. It is not
the whole problem. This project already has a decision on record for
exactly this family: DR-0041, written at the sv2ms face seam one layer
below the wiring shims, found that setjmp/longjmp must never be wrapped in
an out-of-line call-style function, because the pair's contract captures
the literal calling frame -- `%rsp` as it stands and the return address at
`(%rsp)` -- not a value handed to it. A call-style wrapper's `call`
instruction pushes its own return address first, so what gets captured is
the wrapper's frame, not the true caller's. The wrapper returns, its stack
region is reused, and the eventual `longjmp` resumes inside dead, reused
stack. DR-0041 fixed this at the sv2ms seam with `sv2ms-ctx.inc`: a
frameless face that shuffles argument registers and jumps to the body, so
the body captures the real ELF caller's frame directly.

The wiring shim sits above the sv2ms face, translating glibc's ABI to
Cygwin's, but it is exactly the same shape of problem: an ordinary C shim
function that calls Cygwin's real `setjmp` as a normal call captures the
shim's own frame, not the wired veneer's caller. Whether or not the buffer
sizes matched, a call-style shim for this family would already be unsound
for DR-0041's reason, one layer up. The buffer-identity finding was
necessary but not sufficient: solving only the size mismatch and still
reaching Cygwin's `setjmp`/`longjmp` through a call would reproduce the
dead-frame bug DR-0041 exists to prevent.

## Decision

The jmp_buf shims are generated as a frameless face, the wiring-layer
sibling of `sv2ms-ctx.inc`: a jump shape, not the call-then-return shape
`gen-wire.py` already emits for every other shim's thunk body. Concretely,
per direction:

- `setjmp`/`_setjmp`/`sigsetjmp`: load (or lazily allocate, on first use)
  the real 256-byte Cygwin buffer's address from the first eight bytes of
  the caller's 64-byte el8-shaped `jmp_buf` -- the buffer is caller-owned
  storage and opaque to every conforming caller, so stashing a pointer in
  its own first word costs nothing a conforming program can observe --
  place that address in the argument register Cygwin's `setjmp` expects,
  and `jmp` (not `call`) into Cygwin's real entry. Cygwin's body then sees
  the true ELF caller's `%rsp` and return address, exactly as DR-0041
  arranged one seam down.
- `longjmp`/`_longjmp`/`siglongjmp`: load the same stashed pointer, shuffle
  the return-value argument into place, and `jmp` into Cygwin's real
  `longjmp`. Nothing about the restoring jump involves a call frame on
  either side, so no dead-frame window opens on the return path either.

This makes the shim a hand-written frameless thunk alongside the ctx faces,
not a compiled C function `gen-wire.py`'s ordinary shim-call convention
could emit -- the same constraint DR-0041 already established for the
sv2ms seam, now recorded for this seam too so the next increment implements
the right shape the first time rather than discovering the same bug twice.

The buffer-identity questions the prior increment left open are resolved
alongside this:

- **Location**: the real buffer's address lives in the caller's own
  `jmp_buf` (first eight bytes), not a thread-local slot or a side table.
  This needs no nesting bookkeeping -- every live `jmp_buf` already carries
  its own pointer -- and no thread-local storage plumbing this layer does
  not otherwise need.
- **Lazy allocation**: `setjmp` allocates the real buffer only when the
  stashed word is null (a fresh or zero-initialized `jmp_buf`); a `jmp_buf`
  reused across repeated `setjmp` calls (the common `while (setjmp(buf))`
  shape) keeps its one real buffer for its lifetime.
- **Copies**: POSIX does not guarantee a `jmp_buf` survives being copied by
  value and used from the copy -- el8's own mangled, thread-guard-relative
  encoding already breaks that for a caller who copies across threads. A
  copied `jmp_buf` under this scheme carries the same stashed pointer as
  its original, so a `longjmp` through either name reaches the one real
  buffer; two copies used as if independent still collide, matching el8's
  own fragility here rather than adding a new failure mode.
- **Pointer-guard mangling**: not reproduced. El8's `PTR_MANGLE` protects
  against an attacker reading `rsp`/`rip` out of a `jmp_buf` in process
  memory; nothing on this side of the bound table ever reads the mangled
  fields directly; only Cygwin's own `setjmp`/`longjmp`, called through the
  frameless jump, touch the real buffer's bits, under Cygwin's own scheme.
  Reproducing el8's mangling here would protect a representation nothing
  reads.

## Consequences

The runtime slice's remaining five rows move from "open question" to
"specified, not yet built": the next increment writes
`wire-runtime.gen.s`'s hand-authored counterpart to `sv2ms-ctx.inc` (a
`wire-jmpbuf-face.inc` or similar), extends `gen-wire.py` (or a curated
table alongside `ctx.tsv`) to route this family to it instead of the
ordinary shim-call path, and a diff case exercises the lazy-allocation and
repeated-`setjmp` shapes specifically, since those are new behavior this
shim adds that el8's own `jmp_buf` contract does not need to think about.

This also means the shim worklist's two-way split -- "thunk" (tail jump,
no translation) versus "shim" (translation, implied call-style) -- is not
quite the taxonomy the wiring generator has assumed since WP-55: this
family is a third shape, translation without a call. Whoever writes the
generator support should decide whether that is a third generated category
or a curated exception list in the pattern of `ctx.tsv`, and can reopen
this record if the answer changes anything decided here.
