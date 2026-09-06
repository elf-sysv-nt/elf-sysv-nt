# The raw-syscall census

Substrate N carries a rebuilt userland because a `syscall` instruction
on NT is a `#UD` (or a stray call into the wrong kernel), and the only
way to catch it is to not ship it. Proposal 0012 § 8 says the size of
that problem is a list, not a fraction: the packages whose shipped text
carries a raw `syscall` outside glibc, plus the Go runtime, which makes
its own calls and puts its thread pointer where the ABI says. This spike
produces the list.

## Question

Of every x86_64 package in the Rocky 8.10 repositories, how many ship
text that N cannot take as built, because it contains a `syscall`
instruction that is not glibc's, or a Go runtime?

## Method

`run-census.sh` launches `census.py run` detached, resumable, over the
demand census's worklist (every x86_64 package in BaseOS, AppStream,
PowerTools and Extras, less `-devel`, `-doc`, `-static` and similar
names). For each package it fetches the RPM, walks the cpio payload, and
for every ELF file counts `0f 05` byte pairs in the executable `PT_LOAD`
segments and notes whether `.gopclntab` or `.go.buildinfo` is present.
A byte pair is not an instruction, so `measure.sh` runs `census.py
report`, which confirms every byte hit by disassembling the file with
the cross objdump and counting lines that end in `syscall`.

The census writes one fragment per package under a work root in the
untracked annex (`run-census.sh` chooses it; `measure.sh --root` reads
it) and skips packages that have a done marker, so a second run resumes.
It takes hours; the fragments are not tracked.

## Reading

The finding is the shape of the list. `raw-syscall-confined-to-runtimes`
means the packages N cannot take as shipped are at most one in twenty of
those scanned; `raw-syscall-widespread` means more, and 0012 § 8 is
wrong about the size of the rebuild. `glibc-carries-its-own` is always
appended, as a reminder that glibc's own hundreds of `syscall`
instructions are the ones the rebuilt libc replaces, not evidence of
anything.

The raw section lists every package with a confirmed instruction outside
glibc, with the count and whether it is Go, so the list can be read as
a rebuild worklist.

## Files

- `census.py`: `run` (fetch, scan, fragment per package) and `report`
  (aggregate, confirm by disassembly). Shares `rpm_payload`,
  `cpio_files` and `fetch` with `../demand-census/census.py`.
- `run-census.sh`: the detached launcher; safe to rerun.
- `measure.sh`: renders the transcript from the fragments.
- `results-*.txt`: transcripts.
