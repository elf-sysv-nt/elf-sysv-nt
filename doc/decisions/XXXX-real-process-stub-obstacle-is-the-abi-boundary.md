# DR-XXXX — the real-process stub's obstacle is the ABI boundary, not the window

Accepted 2026-09-01. Source: WP-56, the road-to-green `reent-tls-bringup` row,
item 1 of `acceptance/reent/README.md`.

## Context

Item 1 of the reent rung asks that the loader's PE host stub
(`loader/exec/stub.c`) be relinked in the real-process shape -- `-nostdlib`
against the WP-26 `crt0.o` and `-lcygwin` -- so `_dll_crt0` brings the reent up
the sanctioned way. `spike/reent-stub-link` measured that the link succeeds and
the resulting stub faults before its `--version` path, and
`acceptance/reent/stub-realproc.md` recorded the cause as an address collision:
the stub a non-PIE image in the `0x400000` low window it reserves (DR-0028),
`_dll_crt0`'s own low mappings landing on that window before `main`.

`spike/reent-stub-realproc-window` measured that account and it does not hold.
The real-process stub links at `0x100400000`, the ordinary high Cygwin image
base, not `0x400000`; the stub is not in the window it reserves, and the
`--version` path reserves no window at all. What faults is the crt0 startup
crossing. `_cygwin_crt0_common` calls `cygwin_internal(CW_USER_DATA)` with the
Microsoft ABI, and the faced `elfsysv1.dll` exports `cygwin_internal` as a
System V veneer (the WP-27 crossing ABI); reached Microsoft-style, that body
faults before `main`. Interposing one local `cygwin_internal` that re-crosses
the call -- the bridge `spike/reent-bringup`'s real-process probe already
carries -- reaches `main`, and `-DBRIDGE` is the whole difference. Past startup,
one ordinary `printf` -- a Microsoft-ABI call into the faced System V libc --
produces no output while control survives it, so the boundary is not confined to
`cygwin_internal`: it stands at every call the host stub makes into the faced
runtime.

## Decision

The obstacle item 1 names is the Microsoft-to-System-V ABI boundary between a
host PE stub and the faced runtime, not a window or image-base collision.
`acceptance/reent/stub-realproc.md` is corrected to record the ABI crossing as
the measured cause, and `acceptance/reent/README.md` item 1 is reworded to ask
for a stub that crosses the boundary rather than one whose window is reconciled.

Two facts fix what item 1 must now deliver. First, startup itself crosses:
`_cygwin_crt0_common`'s `cygwin_internal` call has to be re-crossed, which a
local interposer does. Second, the stub's own libc use crosses too, so a stub
that keeps calling the faced libc Microsoft-style stays broken past startup; the
real-process stub either confines itself to host-safe calls (kernel32 and
locally defined bodies, the shape the spike probes and `spike/reent-bringup`
use) for its own work, or reaches the faced libc only through the System V
crossing the ELF world already uses. Which of the two the stub takes is left to
the implementing work; this record fixes only that the boundary, not the window,
is what that work reconciles.

The plain-PE stub the WP-41 exec-* certifications drive is untouched. It links
against the unfaced Cygwin and speaks Microsoft to a Microsoft runtime, so the
boundary does not arise for it; the real-process relink is a separate
measurement, not a replacement, and item 3 (a reent-consuming ELF body across
the crossing) stays deferred behind this and the WP-53 `libc.so.6` veneer.

## Consequences

The reent rung's item 1 stops pointing at DR-0028's window contract, which was
not what the measurement found, and points at the ABI boundary, which was. The
correction narrows the work: the startup half has a demonstrated fix, and the
remaining half is a bounded choice about how the stub reaches libc rather than
an open question about address-space layout. No code changes here; the spike,
this record, and the two corrected acceptance notes are the whole of it.
