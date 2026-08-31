# DR-0049 — the wiring crosses through a load-time bound table

Accepted 2026-08-31. Made on the WP-56 branch.

## Context

WP-53's libc and WP-54's companions carry the vendor's symbol surface over
`ret` bodies. WP-56 makes the bodies real: 1937 forwards reach an
`elfsysv1.dll` export and 222 shims translate before reaching one. The ELF
side cannot import from a PE at link time — the two formats share an address
space at run time but no linker crosses them — so something must join a
veneer body to its DLL export while the process is alive, and the choice of
that something is load-bearing for every slice that follows.

## Decision

Each slice gets a generated bind table: one `esn_wire_ent` row per wired
symbol, the export name from the forward map, a function-pointer slot
beside it, hidden visibility because the table is the DSO's own business.
One shared loop, `__esn_wire_bind`, fills the slots at load through a
resolver callback the runtime supplies — in the product, GetProcAddress
over the DLL; in the tests, a fake. A forward's body is a generated thunk,
a rip-relative tail jump through its slot, bound to `symbol@node` by the
same `.symver` arrangement WP-53 binds the stubs it replaces. A shim is a
hand-written body that translates through WP-55's tables and calls through
the same slot, so both kinds cross at exactly one place.

The resolver stays a callback rather than a call into Windows headers so
the table, the loop and the generated code certify under a host compiler
with no cross toolchain and no DLL, which is what `veneer/wiring/t` does.
An unresolved export leaves its slot null and is counted, not fatal: the
192 rows WP-52 flagged as absent stay absent, and the count is the
measurement of how much of a slice is live.

## Alternatives set aside

Binding in the ELF loader's relocation pass would put PE knowledge inside
`loader/`, which has none today and whose resolver (WP-35) is certified
over ELF semantics alone. Generating one giant table for all 2159 wired
rows would work but defeats the slice-by-slice bar: a per-slice table
makes "this slice binds completely" a checkable claim before the next
slice exists.

## Where it is implemented

`veneer/wiring/wire.{h,c}` is the entry shape and the bind loop;
`gen-wire.py` writes each slice's table, thunks and shim worklist from the
forward map and the slice map; `t/test-wire.c` certifies the loop, a
generated table and a rebind over the fixture forward map, and the fixture
thunks assemble for the triple and carry their versioned names.
