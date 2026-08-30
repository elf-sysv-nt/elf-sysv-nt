# elfsysv1.dll API changelog

This file is the discipline the compatibility counter rests on. The counter in
`elfsysv-version.h` is only two numbers; this file is the record of what each
number means, and the rule for when a number moves. A counter without a
changelog is a version nobody can reason about, because there is no way to ask
what an object built against `api 0.4` expected that one built against `api 0.3`
did not.

The form is inherited from Cygwin, which keeps the same list inline in
`winsup/cygwin/include/cygwin/version.h` as a numbered comment against
`CYGWIN_VERSION_API_MINOR` (at `newlib-cygwin` b11613e47 that list runs to
minor 357). The list is kept here as its own file rather than inline because it
is the governing record, not a comment on a macro.

## The three axes

The runtime carries a name and two counters, and they are not the same kind of
thing.

The **generation** is the digit in the name, `elfsysv1`. It is the axis no
counter reaches. A generation break is a runtime a program never loads by
accident, because it resolves the runtime by that name and a different name is a
different file -- the way `cygwin1.dll` and a hypothetical `cygwin2.dll` do not
answer for each other. The digit moves only for a break that no backward-compatible
counter could bridge: a change to the stamp's own shape, the calling convention
at the face, or anything that makes an old program's expectations meaningless
rather than merely unmet. It has never moved; it starts at 1.

The **API major** is the axis reserved for an incompatible change *within* a
generation -- one where old programs would still resolve the runtime by name but
should not run against it. It starts at 0 and has not moved. When it does, the
reasoning belongs in a decision record, not only in a line here.

The **API minor** is the ordinary axis. It is bumped, by one, additively, for
every change to the exported surface a program could have been built to depend
on: a new export, a widened contract, a struct field a program compiled after
the change would read and one compiled before would not. It never decreases and
is never reused. Each bump gets a line below saying what it added.

## Why the pair is compared combined, and backward only

The check folds the pair into one number, `major * 1000 + minor`, and refuses a
program whose combined number is greater than the runtime's. That is the whole
of the rule: a program built against a lower or equal API runs; a program built
against a higher one expects entry points this runtime does not carry, and is
refused with a diagnostic rather than left to fail at the first missing symbol.
It reads on the combined pair rather than on the major alone -- unlike Cygwin,
whose load-time refusal in `check_sanity_and_sync` tests the major and leaves
the minor to feature-gate macros -- because here every additive change bumps the
minor, and a program built after one genuinely needs a runtime that has it.
DR-0018 records that choice.

## Why it starts at the first release, not the first break

The pair is stamped from the first release rather than introduced at the first
incompatible change. Retrofitting a counter after the fact means guessing which
already-shipped binaries predate which change, and there is no honest way to
guess it -- the binaries do not carry the answer. Starting at `0.1` means every
object the runtime ever sees carries a stamp that means something.

## The releases

  0.1: Initial release. The runtime's outward surface is the export list cut
       from Cygwin 3.6.10's `cygwin.din` (DR-0007, `runtime/exports/`), wrapped
       and mapped by WP-21 and WP-51. This is the baseline every later minor is
       measured against; a program stamped `0.1` expects exactly this surface.
