# The compatibility counter (WP-25)

`elfsysv1.dll` inherits Cygwin's backward-compatibility rule, and this package
is the machinery that enforces it: the runtime's own API major and minor
counters, the stamp a program built against the runtime carries, and the check
that reads that stamp at load and decides whether to run the program. The rule
is Cygwin's, down to the digit in the name, so the counter is kept from the
first release rather than introduced at the first break.

The mechanism this package realises is recorded in DR-0018; the changelog
discipline the counters rest on is `CHANGELOG.md`; the reasoning inherited from
Cygwin is read against real source at `newlib-cygwin` b11613e47, not memory.

## What Cygwin does, and what this re-faces

Cygwin's version numbers live in `winsup/cygwin/include/cygwin/version.h`. Three
of its definitions carry the rule:

  - `CYGWIN_VERSION_API_MAJOR` and `CYGWIN_VERSION_API_MINOR` -- the DLL's own
    API version (0 and 357 at b11613e47).
  - `CYGWIN_VERSION_DLL_IDENTIFIER` -- `"cygwin1"`, the string whose trailing
    digit names the runtime generation and appears in the DLL's file name.

A program does not compare these itself. Its startup code
(`_cygwin_crt0_common.cc`) stamps the values that were in effect *when the
program was compiled* into a `per_process` structure -- `magic_biscuit` set to
the structure's size, then `dll_major`, `dll_minor`, `api_major`, `api_minor`.
The DLL reads that structure at load in `check_sanity_and_sync` (`dcrt0.cc`) and
enforces two things in order: the structure size matches (`magic_biscuit`
against `SIZEOF_PER_PROCESS`, a backup against the two sides disagreeing about
the stamp's own shape), and then the version -- `if (p->api_major >
cygwin_version.api_major) api_fatal (...)`. A program built against a newer API
than the DLL provides is refused; the reverse runs. That is the backward-only
rule.

This package re-faces exactly that, with two deliberate differences named below.

## The three axes

`elfsysv-version.h` carries the re-faced equivalents:

  - `ELFSYSV_VERSION_DLL_IDENTIFIER` -- `"elfsysv1"`, the generation. The digit
    is the axis no counter reaches; a different digit is a different DLL a
    program never loads by accident. It starts at 1.
  - `ELFSYSV_VERSION_API_MAJOR` / `ELFSYSV_VERSION_API_MINOR` -- the counter,
    starting at `0.1`. Major is reserved for an incompatible change within a
    generation; minor is bumped additively for every export a program could
    depend on. `CHANGELOG.md` governs both.

The stamp a program carries is `elfsysv_version_stamp`: the struct size as
`magic`, the `api_major`/`api_minor` it was built against, and the generation
identifier. `ELFSYSV_VERSION_STAMP_INIT` expands to the values baked into the
header at the program's compile time, exactly as Cygwin's crt0 copies the macros
into `per_process`. The runtime's own stamp is `elfsysv_runtime_version`, filled
from the same macros -- Cygwin's `cygwin_version`, the fixed right-hand side of
every check.

## The check

`elfsysv_check_compat (program, runtime, diag, diaglen)` in `compat.c` decides,
in `check_sanity_and_sync`'s order:

  1. **Stamp size.** `program->magic != runtime->magic` -> refused. The two
     sides were built against structurally different definitions of the stamp,
     so no field below can be trusted. This is Cygwin's `magic_biscuit` backup.
  2. **Generation.** The identifiers differ -> refused. A different digit is a
     different runtime; the counter does not reach across it. In the field the
     name mismatch would already have kept the two apart at resolution, so this
     is a last line rather than the first.
  3. **Counter, backward only.** `program`'s combined `major * 1000 + minor`
     greater than `runtime`'s -> refused. Otherwise the program runs.

On a refusal, and only then, `diag` is filled with a line naming both versions.
The function returns a verdict and never aborts: the caller owns what a refusal
does, so the runtime can report and exit cleanly rather than crash. That is the
milestone's "a diagnostic rather than a crash".

## The two differences from Cygwin, and why

**The counter is compared combined, not on the major alone.** Cygwin's
load-time refusal tests `api_major` and leaves the minor to feature-gate macros
(`CYGWIN_VERSION_..._COMBINED >= N`). Here the refusal reads the combined pair,
because the minor is where every additive change lands and a program built after
one needs a runtime that has it. Refusing on the combined pair is the honest
reading of "a program built against a lower minor runs against a higher one, and
the reverse is refused" -- the milestone's own done-condition, stated in minors.

**The counter starts at the first release.** Cygwin's history means its counter
is a long retrospective list; ours is stamped from `0.1` so no shipped binary
ever predates the counter. `CHANGELOG.md` explains why retrofitting one cannot
be done honestly.

## Building and testing

    ./t/run.sh

The driver builds `compat.c` and the test with the host gcc and runs the test,
reporting each step through the session monitor when a `.smon` marker sits
above. `-k` keeps the built binary, `-q` is errors only.

The counter is built and run on the host toolchain, not cross-compiled, for the
reason WP-22 certified its crossing on the host toolchain: the code has to run
to produce a verdict. `compat.c` is plain integer and string comparison with no
ABI content and is the same code that links into `elfsysv1.dll`; it uses
`<string.h>` and `<stdio.h>`, which are the runtime's own and resolve under the
cross toolchain once the veneer headers (WP-50) are in its sysroot. Until then a
cross-compile of this file has no headers to find, so the driver does not
attempt one and does not pretend to.

## The test

`t/compat_test.c` models two objects carrying version stamps -- a "program" and
a "runtime" -- and drives the check across the matrix, the way Cygwin's rule is
exercised by a program's `per_process` meeting the DLL's `cygwin_version`. The
two headline directions are checked by name: a program at `0.3` runs against a
runtime at `0.5`, and a program at `0.7` is refused against the same runtime
with its diagnostic printed. The rest of the matrix covers equal versions, the
major axis in both directions, a different generation, the stamp-size backup,
and the shipped `elfsysv_runtime_version` as the right-hand side so the real
value is exercised and not only synthetic ones. A refusal that produced no
diagnostic, or an acceptance that produced one, fails the case: the
done-condition is a diagnostic, not a crash and not a silent no.

## Not verified

That the program's stamp is emitted by real startup code. This package defines
the stamp and the check and proves the check; the crt0 that writes
`ELFSYSV_VERSION_STAMP_INIT` into a program's image the way Cygwin's
`_cygwin_crt0_common.cc` writes `per_process`, and the loader site that reads it
before handing control over, are the startup and loader packages' to wire. The
test stamps its objects directly to stand in for that emission, exactly as far
as a runtime that does not yet fully exist allows.

That the combined-comparison choice matches every future need. It is the right
reading of the rule as stated and as the minor is used today; a change that made
the major the only incompatible axis, with the minor purely informational, would
reopen DR-0018 rather than edit this file.
