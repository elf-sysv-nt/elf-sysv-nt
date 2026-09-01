# WIP — wiring the dynamic crossing into the stub

WP-56, the acceptance step. The wiring and live-crossing phases are complete
and the acceptance embryo reads bzip2 as `ready`: every libc symbol has a
certified body behind it. What remains is the overall done-when — a vendor
package built against this tree runs its own test suite and passes — and its
first, load-bearing piece is the one this branch adds.

The dynamic crossing driver `dyn_exec_link` (DR-0058) is written and certified
over a real cross-built object pair in `t/dyn_exec_test.c`. But the stub does
not yet call it: a dynamic image reaches `stub.c`'s exec-kind branch and is
refused there, with a note that the crossing "is not yet wired into the stub."
This branch replaces that refusal with the crossing.

For a dynamic image the stub now: maps an ELF runtime supplied by a new option,
composes it with the mapped main image through `dyn_exec_link` so the main
image's GOT and PLT resolve against the runtime's exports, and enters at
`e_entry` over the SysV stack the process builder already laid down. Running
the main image's `DT_INIT`/`DT_INIT_ARRAY` between the link and the entry —
WP-33's order, `dl_run_init` — is the increment that follows this one; a
specimen with no initializers does not need it and bzip2 does.

The certification drives a cross-built dynamic specimen — an interp-bearing
`ET_EXEC` with no initializers that calls across an object boundary into an ELF
runtime and exits with what the call returns — through the real stub and front
end, the same way the WP-41 exec-elf check drives its specimen, and holds the
exit status to the value only a correctly relocated cross-object call produces.

The ELF runtime the stub links against here is a bare specimen, not yet the
WP-53 `libc.so.6` veneer bound through `wire.c` into `elfsysv1.dll`. Standing
the stub's dynamic branch up against a specimen first is the leaf; mapping the
veneer and running its bind loop against the PE runtime, then bzip2 itself, are
the trunk-ward steps this one carries.
